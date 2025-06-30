#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <sys/inotify.h>
#include <unistd.h>

#include "postgres.h"

#include "access/heapam_xlog.h"
#include "access/xact.h"
#include "access/xlog_internal.h"
#include "utils/elog.h"

#include "cache_serializer.h"
#include "cache.h"
#include "config.h"
#include "invalid.h"
#include "wal_reader.h"

#define MAX_EVENTS 100
#define LEN_NAME 25
#define EVENT_SIZE  (sizeof (struct inotify_event))
#define BUF_LEN     (MAX_EVENTS * (EVENT_SIZE + LEN_NAME))

#define XLOGDIR	"pg_wal"

#define zero_byte 1

extern config_cache config;
int wd;
int wal_reader_fd;

static int init_notifier(void) {
    wal_reader_fd = inotify_init1(0);
    if (wal_reader_fd == -1) {
        char* emsg = strerror(errno);
        ereport(ERROR, errmsg("init_wal_reader: can't inotify_init1: %s", emsg));
        abort();
    }

    wd = inotify_add_watch(wal_reader_fd, XLOGDIR, IN_MODIFY);
    if (wd == -1) {
        char* emsg = strerror(errno);
        ereport(ERROR, errmsg("init_wal_reader: can't inotify_add_watch: %s", emsg));
        abort();
    }
    return wal_reader_fd;
}

static void finish_nofier (void) {
    inotify_rm_watch(wal_reader_fd, wd);
    close(wal_reader_fd);
}

static void get_record(XLogReaderState* xlogreader) {
	size_t datalen;
    xl_heap_update* xlrec;
    BlockNumber newblk;
    RelFileLocator rlocator;
    key_info* key_i;

    char* recdata = XLogRecGetBlockData(xlogreader, 0, &datalen);

    xlrec = (xl_heap_update*)XLogRecGetData(xlogreader);
    XLogRecGetBlockTag(xlogreader, 0, &rlocator, NULL, &newblk);

    if (xlrec->flags & XLH_UPDATE_PREFIX_FROM_OLD) {
		recdata += sizeof(uint16);
        datalen -= sizeof(uint16);
	}
	if (xlrec->flags & XLH_UPDATE_SUFFIX_FROM_OLD) {
		recdata += sizeof(uint16);
        datalen -= sizeof(uint16);
	}
	recdata += SizeOfHeapHeader;
    datalen -= SizeOfHeapHeader;

    recdata += zero_byte;
    datalen -= SizeOfHeapHeader;

    key_i = create_key_info_by_record(rlocator.relNumber, recdata);
    if (config.c_conf.invalid_update) {
        created_cache_respons* ccr =  create_respons_by_xlog(recdata, datalen, rlocator.relNumber);
        invalidate_cache(key_i, ccr->res, ccr->size, INV_UPDATE);
    } else {
        invalidate_cache(key_i, NULL, 0, INV_DELETE);
    }
    return;
}

static void process_update(XLogReaderState* xlogreader) {
    get_record(xlogreader);
}

static void process_heap(XLogRecord*  record, XLogReaderState* xlogreader) {
	elog(INFO, "process_heap:start");
    char info = record->xl_info & ~XLR_INFO_MASK;
    switch (info & XLOG_HEAP_OPMASK) {
		case XLOG_HEAP_DELETE:
			elog(INFO, "process_heap: XLOG_HEAP_DELETE");
			break;
		case XLOG_HEAP_UPDATE:
            process_update(xlogreader);
			elog(INFO, "process_heap: XLOG_HEAP_UPDATE");
			break;
		case XLOG_HEAP_TRUNCATE:
			elog(INFO, "process_heap: XLOG_HEAP_TRUNCATE");
			break;
		case XLOG_HEAP_HOT_UPDATE:
            process_update(xlogreader);
			elog(INFO, "process_heap: XLOG_HEAP_HOT_UPDATE");
			break;
    }
}

static void process_xact(XLogRecord*  record, XLogReaderState* xlogreader) {
    char info = XLogRecGetInfo(xlogreader) & XLOG_XACT_OPMASK;
    switch (info) {
        case XLOG_XACT_COMMIT:
			elog(INFO, "process_xact: XLOG_XACT_COMMIT");
            break;
        case XLOG_XACT_ABORT:
			elog(INFO, "process_xact: XLOG_XACT_ABORT");
            break;
    }
}

static void process_record(XLogRecord*  record, XLogReaderState* xlogreader) {
    RmgrData rmgr = GetRmgr(record->xl_rmid);
    if (strncmp(rmgr.rm_name, "Heap", 4 ) == 0) {
        process_heap(record, xlogreader);
    } else if (strncmp(rmgr.rm_name, "Transaction", 12 ) == 0) {
        process_xact(record, xlogreader);
    }
}

static void* start_invalidator(void* arg) {
    char buffer[BUF_LEN];
    int wal_reader_fd = init_notifier();
    wal_info* wal = init_wal_info();

    while (1) {
        int length = read(wal_reader_fd, buffer, BUF_LEN );
        int cur_index = 0;

        while (cur_index < length) {
            struct inotify_event* event = (struct inotify_event*) &(buffer[cur_index]);
            if (event->len && event->mask == IN_MODIFY) {
                while (true) {
                    XLogRecord*  record = read_next_XLog_record(wal);
                    if (!record) {
                        break;
                    }
                    process_record(record, wal->xlogreader);
                }
            }
            cur_index += EVENT_SIZE + event->len;
        }
    }
    finish_nofier();
    destroy_wal_info(wal);
    return NULL;
}

void init_invalidator(void) {
    pthread_t wr_tid;
    int err = pthread_create(&(wr_tid), NULL, start_invalidator, NULL);
    if (err) {
        ereport(INFO, errmsg("init_cache_gc: pthread_create error %s", strerror(err)));
        abort();
    }
}
