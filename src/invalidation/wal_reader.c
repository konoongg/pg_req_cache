#include <assert.h>
#include <unistd.h>

#include "postgres.h"

#include "access/xlog.h"
#include "access/xlogreader.h"
#include "access/xlogrecovery.h"
#include "access/xlogutils.h"
#include "pg_config.h"
#include "server/replication/slot.h"

#include "alloc.h"
#include "logger.h"
#include "wal_reader.h"

static XLogRecPtr get_cur_LSN(void) {
	XLogRecPtr	star_lsn;
	if (!RecoveryInProgress()) {
		star_lsn = GetFlushRecPtr(NULL);
    }
	else {
		star_lsn = GetXLogReplayRecPtr(NULL);
    }

	Assert(!XLogRecPtrIsInvalid(star_lsn));
	return star_lsn;
}

bool is_valid_LSN(XLogRecPtr read_lsn) {
	XLogRecPtr	processed_lsn = get_cur_LSN();
	return !(processed_lsn < read_lsn);
}

static XLogReaderState* init_XLog_reader_state(XLogRecPtr lsn)
{
	XLogReaderState *xlogreader;
	ReadLocalXLogPageNoWaitPrivate* private_data;

	if (lsn < XLOG_BLCKSZ) {
		cache_log(CACHE_ERROR, "could not read WAL at LSN %X/%X  lsn(%ld) < XLOG_BLCKSZ(%d) ", LSN_FORMAT_ARGS(lsn), lsn, XLOG_BLCKSZ);
    }

	private_data = (ReadLocalXLogPageNoWaitPrivate *) wcalloc(sizeof(ReadLocalXLogPageNoWaitPrivate));

	xlogreader = XLogReaderAllocate(wal_segment_size, NULL,
									XL_ROUTINE(.page_read = &read_local_xlog_page,
											   .segment_open = &wal_segment_open,
											   .segment_close = &wal_segment_close),
									private_data);

	if (xlogreader == NULL) {
		cache_log(CACHE_ERROR, "out of memory. Failed while allocating a WAL reading processor.");
    }

	return xlogreader;
}

XLogRecord* read_next_XLog_record(wal_info* wal) {
	XLogRecord *record;
	char	   *errormsg;
	XLogReaderState* xlogreader = wal->xlogreader;
	XLogRecPtr	first_valid_record = InvalidXLogRecPtr;

	if (xlogreader->NextRecPtr == 0) {
		first_valid_record = XLogFindNextRecord(xlogreader, wal->start_lsn);
		if (XLogRecPtrIsInvalid(first_valid_record)) {
			cache_log(CACHE_ERROR, "read_next_XLog_record: can't find first valid record");
		}
	}

	record = XLogReadRecord(xlogreader, &errormsg);
	return record;
}

wal_info* init_wal_info (void) {
    wal_info* wal = wcalloc(sizeof(wal_info));
    wal->start_lsn = get_cur_LSN();
    wal->xlogreader = init_XLog_reader_state(wal->start_lsn);
	memcpy(wal->slot_name, CACHE_SLOT_NAME, SLOT_NAME_SIZE);

	ReplicationSlotCreate(wal->slot_name, false, RS_EPHEMERAL, false, false, false);
	update_slot_position(wal->slot_name, wal->start_lsn);
    return wal;
}

void destroy_wal_info(wal_info* wal) {
    free(wal);
}

void update_slot_position(const char *slot_name, XLogRecPtr lsn) {
   	ReplicationSlot *slot = SearchNamedReplicationSlot(slot_name, false);
    if (slot == NULL) {
        cache_log(CACHE_WARNING, "Replication slot '%s' not found", slot_name);
        return;
    }

	SpinLockAcquire(&slot->mutex);

	slot->data.restart_lsn = lsn;
    slot->data.confirmed_flush = lsn;

	SpinLockRelease(&slot->mutex);
}