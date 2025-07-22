#include "postgres.h"

#include "access/heapam_xlog.h"
#include "access/transam.h"
#include "access/xact.h"
#include "access/xlog.h"
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

            add_trans_event(key_i, res, get_xid_from_querydesc(queryDesc), false);

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
    created_cache_respons* res;
    size_t data_size;

    /*
    * if shared memory is not initialized,
    * it means that the kzhsh is not yet ready to accept requests,
    * which means there is nothing to invalidate
    */
    if  (!load_shared_struct()) {
        return;
    }

    XLogRecGetBlockTag(xlogreader, 0, &rlocator, NULL, NULL);

    if (!table_filter(rlocator.relNumber)) {
        return;
    }

    recdata = XLogRecGetBlockData(xlogreader, 0, &data_size);
    if (recdata == NULL) {
        cache_log(CACHE_WARNING, "can't read wal data");
        return;
    }

    // тут не хватате првоеркуи на ошибки
    xlrec = (xl_heap_update*)XLogRecGetData(xlogreader);

    if (xlrec->flags & XLH_UPDATE_PREFIX_FROM_OLD) {
		recdata += sizeof(uint16);
        data_size -= sizeof(uint16);
	}

	if (xlrec->flags & XLH_UPDATE_SUFFIX_FROM_OLD) {
		recdata += sizeof(uint16);
        data_size -= sizeof(uint16);
	}

	recdata += SizeOfHeapHeader;
    data_size -= SizeOfHeapHeader;

    recdata += SKIP_BYTE;
    data_size -= SKIP_BYTE;

    key_i = create_key_info_by_record(rlocator.relNumber, recdata);

    res = create_respons_by_xlog(recdata, data_size, rlocator.relNumber);

    add_trans_event(key_i, res, XLogRecGetXid(xlogreader), true);

    cache_log(CACHE_DEBUG, "inv_process_record_update add inv %d", XLogRecGetXid(xlogreader));
    free(res);
    destroy_key_info(key_i);
}

void inv_process_xac_commit(XLogReaderState* xlogreader) {
    cache_log(CACHE_DEBUG, "inv_process_xac_commit xid  %d", XLogRecGetXid(xlogreader));

    if (!load_shared_struct()) {
        return;
    }

    process_apply(XLogRecGetXid(xlogreader));
}

void inv_process_xact(XactEvent event, void* arg) {
    TransactionId xid;
    if (RecoveryInProgress()) {
        return;
    }

    xid = GetCurrentTransactionIdIfValid();
    if (xid == InvalidTransactionId) {
        return;
    }

    if (!load_shared_struct()) {
        return;
    }

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
