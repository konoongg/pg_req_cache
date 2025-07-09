#include <stdlib.h>
#include <unistd.h>

#include "postgres.h"

#include "fmgr.h"
#include "postmaster/bgworker.h"

#include "alloc.h"
#include "cache_gc.h"
#include "cache.h"
#include "command_processor.h"
#include "config.h"
#include "invalidation/invalid.h"
#include "logger.h"
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
    worker.bgw_start_time =  BgWorkerStart_ConsistentState;
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
    cache_log(CACHE_INFO, "start bg worker pg_redis_proxy pid");

    init_config();
    cache_log(CACHE_INFO, "finish init config");

    init_stats();
    cache_log(CACHE_INFO, "finish init stats");

    init_def_resp();

    init_commands();
    cache_log(CACHE_INFO, "finish init commands");

    init_cache();
    cache_log(CACHE_INFO, "finish init cache");

    init_cache_gc();
    cache_log(CACHE_INFO, "finish init cache gc");

    cache_log(CACHE_INFO, "start init db worker");
    init_db_worker();
    cache_log(CACHE_INFO, "finish init db worker %d", config.worker_conf.count_worker);

    //init_invalidator();
    cache_log(CACHE_INFO, "finish init wal reader");

    init_workers();
}
