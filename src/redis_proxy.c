#include <stdlib.h>
#include <unistd.h>

#include "postgres.h"

#include "fmgr.h"
#include "postmaster/bgworker.h"
#include "utils/elog.h"

#include "alloc.h"
#include "cache_gc.h"
#include "cache.h"
#include "command_processor.h"
#include "config.h"
#include "invalidation/invalid.h"
#include "query_cache_controller.h"
#include "resp_creater.h"
#include "socket_wrapper.h"
#include "stats.h"
#include "worker.h"

PG_MODULE_MAGIC;

void _PG_init(void);
static void register_proxy(void);
PGDLLEXPORT void proxy_start_work(Datum main_arg);
void clean_up(void);

config_cache config;
statistics stats;

void _PG_init(void) {
    register_proxy();
}

static void register_proxy(void) {
    BackgroundWorker worker;
    memset(&worker, 0, sizeof(BackgroundWorker));
    worker.bgw_flags = BGWORKER_SHMEM_ACCESS;
    worker.bgw_start_time =  BgWorkerStart_RecoveryFinished;
    strncpy(worker.bgw_library_name, "pg_redis_proxy", 15);
    strncpy(worker.bgw_function_name, "proxy_start_work", 17);
    strncpy(worker.bgw_name, "pg_redis_proxy", 15);
    strncpy(worker.bgw_type, "redis proxy server", 19);
    worker.bgw_restart_time = BGW_NEVER_RESTART;
    RegisterBackgroundWorker(&worker);
}


/*
* The application is initializing,
* the background worker required for database synchronization is starting,
* and the I/O workers, which handle the main tasks, are being launched.
*/
void proxy_start_work(Datum main_arg) {
    ereport(INFO, errmsg("start bg worker pg_redis_proxy pid: %d", getpid()));

    init_config();
    ereport(INFO, errmsg("finish init config"));

    init_stats();
    ereport(INFO, errmsg("finish init stats"));

    init_def_resp();

    init_commands();
    ereport(INFO, errmsg("finish init commands"));

    init_cache();
    ereport(INFO, errmsg("finish init cache"));

    //init_cache_gc();
    ereport(INFO, errmsg("finish init cache gc"));

    //init_invalidator();
    ereport(INFO, errmsg("finish init wal reader"));

    ereport(INFO, errmsg("start init db worker"));
    init_db_worker();
    ereport(INFO, errmsg("finish init db worker %d", config.worker_conf.count_worker));

    init_workers();
}
