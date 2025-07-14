#include "postgres.h"

#include "access/transam.h"
#include "access/xact.h"
#include "executor/executor.h"

#include "invalid.h"
#include "logger.h"

static TransactionId get_xid_from_querydesc(QueryDesc *queryDesc) {
    if (queryDesc && queryDesc->estate) {
        return queryDesc->estate->es_snapshot->xmin;  // Или xmax, в зависимости от ситуации
    }
    return InvalidTransactionId;
}

void inv_process_command(QueryDesc* queryDesc) {
    switch (queryDesc->operation) {
        case(CMD_UPDATE):
            cache_log(CACHE_DEBUG, "INV_UPDATE %d", get_xid_from_querydesc(queryDesc));
            break;
        case(CMD_DELETE):
            cache_log(CACHE_DEBUG, "INV_DELETE %d", get_xid_from_querydesc(queryDesc));
            break;
        default:
            break;
    }
}

void inv_process_xact(XactEvent event, void* arg) {
    TransactionId xid;

    cache_log(CACHE_DEBUG, "IsTransactionState() %d event %d", IsTransactionState(), event);

    if (!IsTransactionState()) {
        return;
    }

    xid = GetCurrentTransactionId();
    switch (event) {
        case (XACT_EVENT_COMMIT):
            cache_log(CACHE_DEBUG, "xact commit %d", xid);
            break;
        case (XACT_EVENT_ABORT):
            cache_log(CACHE_DEBUG, "xact abort %d", xid);
            break;
        default:
            cache_log(CACHE_DEBUG, "event %d", event);
            break;
    }
}