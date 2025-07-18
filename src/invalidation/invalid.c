#include "postgres.h"

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

            cache_log(CACHE_DEBUG, "cache set from invalid");
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

void inv_process_xact(XactEvent event, void* arg) {
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
            cache_log(CACHE_DEBUG, "xact abort %d", xid);
            break;
        default:
            break;
    }
}
