#include <assert.h>

#include "postgres.h"

#include "access/xlog.h"
#include "access/xlogreader.h"
#include "access/xlogrecovery.h"
#include "access/xlogutils.h"
#include "pg_config.h"
#include "utils/elog.h"

#include "alloc.h"
#include "wal_reader.h"



static XLogRecPtr get_start_LSN(void) {
	XLogRecPtr	star_lsn;
	if (!RecoveryInProgress()) {
		star_lsn = GetFlushRecPtr(NULL);
    }
	else {
		star_lsn = GetXLogReplayRecPtr(NULL);
    }

	Assert(!XLogRecPtrIsInvalid(star_lsn));
	ereport(INFO, errmsg("get_start_LSN  %ld", star_lsn));
	return star_lsn;
}


static XLogReaderState* init_XLog_reader_state(XLogRecPtr lsn)
{
	XLogReaderState *xlogreader;
	ReadLocalXLogPageNoWaitPrivate* private_data;

	if (lsn < XLOG_BLCKSZ) {
		ereport(ERROR, errmsg("could not read WAL at LSN %X/%X  lsn(%ld) < XLOG_BLCKSZ(%d) ", LSN_FORMAT_ARGS(lsn), lsn, XLOG_BLCKSZ));
        abort();
    }

	private_data = (ReadLocalXLogPageNoWaitPrivate *) wcalloc(sizeof(ReadLocalXLogPageNoWaitPrivate));

	xlogreader = XLogReaderAllocate(wal_segment_size, NULL,
									XL_ROUTINE(.page_read = &read_local_xlog_page_no_wait,
											   .segment_open = &wal_segment_open,
											   .segment_close = &wal_segment_close),
									private_data);

	if (xlogreader == NULL) {
		ereport(ERROR,
				(errcode(ERRCODE_OUT_OF_MEMORY),
				 errmsg("out of memory"),
				 errdetail("Failed while allocating a WAL reading processor.")));
        abort();
    }

	return xlogreader;
}

XLogRecord* read_next_XLog_record(wal_info* wal) {
	XLogRecord *record;
	char	   *errormsg;
	XLogReaderState* xlogreader = wal->xlogreader;
	XLogRecPtr	first_valid_record = InvalidXLogRecPtr;

	if (xlogreader->NextRecPtr == 0) {
		while (XLogRecPtrIsInvalid(first_valid_record)) {
			first_valid_record = XLogFindNextRecord(xlogreader, wal->start_lsn);
			__asm__ __volatile__("pause");
			ereport(INFO, (errmsg("could not find a valid record after %X/%X", LSN_FORMAT_ARGS(wal->start_lsn))));
		}
	}

	record = XLogReadRecord(xlogreader, &errormsg);

	if (record == NULL)	{

		if (errormsg) {
            ereport(ERROR, (errcode_for_file_access(),  errmsg("could not read WAL at %X/%X: %s", LSN_FORMAT_ARGS(xlogreader->EndRecPtr), errormsg)));
            abort();
        } else {
			__asm__ __volatile__("pause");
			record = XLogReadRecord(xlogreader, &errormsg);
        }
	}
	return record;
}

wal_info* init_wal_info (void) {
    wal_info* wal = wcalloc(sizeof(wal_info));
    wal->start_lsn = get_start_LSN();
    wal->xlogreader = init_XLog_reader_state(wal->start_lsn);
    return wal;
}

void destroy_wal_info(wal_info* wal) {
    free(wal);
}