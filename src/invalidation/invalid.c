#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <sys/inotify.h>
#include <unistd.h>

#include "postgres.h"

#include "access/heapam_xlog.h"
#include "access/htup_details.h"
#include "access/xact.h"
#include "access/xlog_internal.h"

#include "alloc.h"
#include "cache_serializer.h"
#include "cache.h"
#include "config.h"
#include "invalid_pool.h"
#include "invalid.h"
#include "logger.h"
#include "meta_db.h"
#include "wal_reader.h"

#define MAX_EVENTS 100
#define LEN_NAME 25
#define EVENT_SIZE  (sizeof (struct inotify_event))
#define BUF_LEN     (MAX_EVENTS * (EVENT_SIZE + LEN_NAME))

#define XLOGDIR	"pg_wal"

#define SKIP_BYTE 1

extern config_cache config;
int wd;
int wal_reader_fd;


static void process_event(key_info* key_i, size_t xid) {
    ht_data* data = prepare_inv_cache(key_i, xid);
    if (data == NULL) {
        return;
    }
    add_xid_event(key_i, xid, data);
}


static int init_notifier(void) {
    wal_reader_fd = inotify_init1(0);
    if (wal_reader_fd == -1) {
        cache_log(CACHE_ERROR,"init_wal_reader: can't inotify_init1: %s", strerror(errno));
    }

    wd = inotify_add_watch(wal_reader_fd, XLOGDIR, IN_MODIFY | IN_CREATE);
    if (wd == -1) {
        cache_log(CACHE_ERROR,"init_wal_reader: can't inotify_add_watch: %s", strerror(errno));
    }
    return wal_reader_fd;
}

static void finish_nofier (void) {
    inotify_rm_watch(wal_reader_fd, wd);
    close(wal_reader_fd);
}

static void process_update(XLogRecord*  record, XLogReaderState* xlogreader) {
    xl_heap_update* xlrec;
    RelFileLocator rlocator;
    key_info* key_i;
    char* recdata;

    XLogRecGetBlockTag(xlogreader, 0, &rlocator, NULL, NULL);

    if (!table_filter(rlocator.relNumber)) {
        return;
    }

    recdata = XLogRecGetBlockData(xlogreader, 0, NULL);
    if (recdata == NULL) {
        cache_log(CACHE_WARNING, "can't read wal data");
        return;
    }

    // тут не хватате првоеркуи на ошибки
    xlrec = (xl_heap_update*)XLogRecGetData(xlogreader);

    if (xlrec->flags & XLH_UPDATE_PREFIX_FROM_OLD) {
		recdata += sizeof(uint16);
	}

	if (xlrec->flags & XLH_UPDATE_SUFFIX_FROM_OLD) {
		recdata += sizeof(uint16);
	}

	recdata += SizeOfHeapHeader;

    recdata += SKIP_BYTE;

    key_i = create_key_info_by_record(rlocator.relNumber, recdata);

    if (key_i) {
        process_event(key_i, record->xl_xid);
    }

    destroy_key_info(key_i);
}


// the current implementation of invalidation on deletion only works when a private key is set
static void process_delete(XLogRecord* record, XLogReaderState* xlogreader) {
    xl_heap_delete* xlrec = (xl_heap_delete *)XLogRecGetData(xlogreader);
    RelFileLocator rlocator;
    BlockNumber blkno;

    XLogRecGetBlockTag(xlogreader, 0, &rlocator, NULL, &blkno);

    if (!table_filter(rlocator.relNumber)) {
        return;
    }

    if (xlrec->flags & XLH_DELETE_CONTAINS_OLD) {
        key_info* key_i = NULL;
        char* recdata = (char*)xlrec + SizeOfHeapDelete + SizeOfHeapHeader + SKIP_BYTE;
        key_i = create_key_info_by_record(rlocator.relNumber, recdata);

        if (key_i) {
            process_event(key_i, record->xl_xid);
        }

        destroy_key_info(key_i);

    } else {
        cache_log(CACHE_WARNING, "process_delete: the record dosen't have information about a key");
    }
}

static void process_heap(XLogRecord* record, XLogReaderState* xlogreader) {
    char info = record->xl_info & ~XLR_INFO_MASK;
    switch (info & XLOG_HEAP_OPMASK) {
		case XLOG_HEAP_DELETE:
            process_delete(record, xlogreader);
			break;
		case XLOG_HEAP_UPDATE:
            process_update(record, xlogreader);
			break;
		case XLOG_HEAP_HOT_UPDATE:
            process_update(record, xlogreader);
			break;
    }
}

static void process_xact(XLogRecord*  record, XLogReaderState* xlogreader) {
    char info = XLogRecGetInfo(xlogreader) & XLOG_XACT_OPMASK;
    switch (info) {
        case XLOG_XACT_COMMIT:
            process_apply(record->xl_xid);
            break;
        case XLOG_XACT_ABORT:
            process_reset(record->xl_xid);
            break;
    }
}

static void process_record(XLogRecord*  record, XLogReaderState* xlogreader) {
    RmgrData rmgr = GetRmgr(record->xl_rmid);
    if (strcmp(rmgr.rm_name, "Heap") == 0 && strlen(rmgr.rm_name) == 4) {
        process_heap(record, xlogreader);
    } else if (strcmp(rmgr.rm_name, "Transaction") == 0) {
        process_xact(record, xlogreader);
    }
    update_slot_position(CACHE_SLOT_NAME, xlogreader->EndRecPtr);
}

static void* start_invalidator(void* arg) {
    char buffer[BUF_LEN];
    int wal_reader_fd = init_notifier();
    wal_info* wal = init_wal_info();
    XLogRecord*  prepare_record = NULL;

    init_inv_pool();

    while (true) {
        int length = read(wal_reader_fd, buffer, BUF_LEN );
        int cur_index = 0;
        while (cur_index < length) {
            struct inotify_event* event = (struct inotify_event*) &(buffer[cur_index]);
            if (event->len && event->mask == IN_MODIFY) {
                while (true) {
                    XLogRecord*  record;

                    /*
                    * It is possible that we read data from WAL earlier than startup,
                    * that is, there will not be a valid value in the database yet.
                    * Therefore, we need to wait until the record with the corresponding
                    * LSN is applied before invalidating the cache. This ensures we don't
                    * invalidate cache entries for data that hasn't been committed yet.
                    * The synchronization between WAL reading and cache invalidation
                    * is crucial for maintaining data consistency.
                    */

                    if (!prepare_record) {
                        record = read_next_XLog_record(wal);
                        if (!record) {
                            break;
                        }
                    } else {
                        record = prepare_record;
                    }

                    if (is_valid_LSN(wal->xlogreader->ReadRecPtr)) {
                        process_record(record, wal->xlogreader);
                    } else {
                        prepare_record = record;
                        usleep(1000);
                    }
                }
            }
            cur_index += EVENT_SIZE + event->len;
        }
        cache_log(CACHE_DEBUG, "start_invalidator: process all");
    }

    finish_nofier();
    destroy_wal_info(wal);
    return NULL;
}

void init_invalidator(void) {
    pthread_t wr_tid;
    int err = pthread_create(&(wr_tid), NULL, start_invalidator, NULL);
    if (err) {
        cache_log(CACHE_ERROR,"init_cache_gc: pthread_create error %s", strerror(err));
    }
}
