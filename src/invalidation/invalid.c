#include "postgres.h"

#include "access/heapam_xlog.h"
#include "access/transam.h"
#include "access/xact.h"
#include "executor/executor.h"

#include "cache_serializer.h"
#include "invalid_trans.h"
#include "invalid.h"
#include "logger.h"
#include "parse_pg_command.h"
#include "shmem.h"

extern db_meta_data* meta;

#define SKIP_BYTE 1

static TransactionId get_xid_from_querydesc(QueryDesc* queryDesc) {
    if (queryDesc && queryDesc->estate) {
        return queryDesc->estate->es_snapshot->xmin;
    }
    return InvalidTransactionId;
}

void inv_process_command(QueryDesc* queryDesc) {
    const char* command = queryDesc->sourceText;

    load_shared_struct();

    switch (queryDesc->operation) {
        case(CMD_UPDATE):
            pg_parse_data* req = parse_update(command);
            key_info* key_i = create_key_info_by_pg_command(req);
            created_cache_respons* res = create_response_by_pg_command(req);

            add_trans_event(key_i, res, get_xid_from_querydesc(queryDesc));

            free(res);
            destroy_key_info(key_i);
            destroy_parse_data(req);
            break;
        case(CMD_DELETE):
            break;
        default:
            break;
    }
}

void inv_process_record_update(XLogReaderState* xlogreader) {
    xl_heap_update* xlrec;
    RelFileLocator rlocator;
    key_info* key_i;
    char* recdata;

    cache_log(CACHE_DEBUG, "inv_process_record_update start");

    cache_log(CACHE_DEBUG, "inv_process_record_update load_shared_struct start");
    load_shared_struct();
    cache_log(CACHE_DEBUG, "inv_process_record_update XLogRecGetBlockTag start");
    XLogRecGetBlockTag(xlogreader, 0, &rlocator, NULL, NULL);
    cache_log(CACHE_DEBUG, "inv_process_record_update XLogRecGetBlockTag finish");

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
        cache_log(CACHE_DEBUG, "process_record_update: key %s", key_i->full_key);
    }

    destroy_key_info(key_i);
}

void inv_process_xact(XactEvent event, void* arg) {
    cache_log(CACHE_DEBUG, "inv_process_xact: start");

    if (RecoveryInProgress()) {
        return;
    }

    TransactionId xid = GetCurrentTransactionIdIfValid();
    if (xid == InvalidTransactionId) {
        return;
    }

    load_shared_struct();

    switch (event) {
        case (XACT_EVENT_COMMIT):
            process_apply(xid);
            break;
        case (XACT_EVENT_ABORT):
            break;
        default:
            break;
    }
}
