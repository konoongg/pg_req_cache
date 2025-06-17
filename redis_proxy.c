#include <stdlib.h>
#include <unistd.h>

#include "postgres.h"

#include "fmgr.h"
#include "miscadmin.h"
#include "postmaster/bgworker.h"
#include "storage/ipc.h"
#include "storage/shmem.h"
#include "utils/elog.h"

#include "alloc.h"
#include "cache_gc.h"
#include "cache.h"
#include "cache.h"
#include "command_processor.h"
#include "config.h"
#include "invalid.h"
#include "query_cache_controller.h"
#include "resp_creater.h"
#include "socket_wrapper.h"
#include "stats.h"
#include "worker.h"

PG_MODULE_MAGIC;

void _PG_init(void);
static void register_proxy(void);
PGDLLEXPORT void req_cache_start_work(Datum main_arg);
void clean_up(void);

config_redis config;
cache* c;
statistics stats;

static shmem_request_hook_type prev_shmem_request_hook = NULL;
static shmem_startup_hook_type prev_shmem_startup_hook = NULL;

static void req_cache_shmem_request(void) {
    int shmem_size;
    
    if (prev_shmem_request_hook) {
		prev_shmem_request_hook();
    }
    shmem_size = sizeof(cache) + config.c_conf.max_storage_size + config.c_conf.metadata_memory_size;
    RequestAddinShmemSpace(shmem_size);
}

static void req_cache_shmem_startup(void) {
    bool found;
    int shmem_size;

    if (prev_shmem_startup_hook) {
        prev_shmem_startup_hook();
    }

    shmem_size = sizeof(cache) + config.c_conf.max_storage_size + config.c_conf.metadata_memory_size;

    ereport(INFO, errmsg("start init shmem with sise: %d", shmem_size));

    c = ShmemInitStruct("ReqCacheSharedMemory", shmem_size - sizeof(cache) , &found);

    ereport(INFO, errmsg("finish init shmem with sise: %d", shmem_size));
    init_shalloc(c + sizeof(cache), shmem_size - sizeof(cache));

    ereport(INFO, errmsg("start init_shalloc"));
    if (!found) {
        memset(c, 0, sizeof(cache));
        ereport(INFO, errmsg("start init cache"));
        init_cache(c);
        ereport(INFO, errmsg("finish init cache"));
    }
    ereport(INFO, errmsg("shmem has inited"));
}


void _PG_init(void) {
    init_config();
    ereport(INFO, errmsg("finish init config"));

    prev_shmem_request_hook = shmem_request_hook;
    shmem_request_hook = req_cache_shmem_request;
    prev_shmem_startup_hook = shmem_startup_hook;
    shmem_startup_hook = req_cache_shmem_startup;
    register_proxy();
}

static void register_proxy(void) {
    BackgroundWorker worker;
    memset(&worker, 0, sizeof(BackgroundWorker));
    worker.bgw_flags = BGWORKER_SHMEM_ACCESS;
    worker.bgw_start_time =  BgWorkerStart_RecoveryFinished;
    strncpy(worker.bgw_library_name, "pg_redis_proxy", 15);
    strncpy(worker.bgw_function_name, "req_cache_start_work", 21);
    strncpy(worker.bgw_name, "pg_redis_proxy", 15);
    strncpy(worker.bgw_type, "req cache  server", 18);
    worker.bgw_restart_time = BGW_NEVER_RESTART;
    RegisterBackgroundWorker(&worker);
}


/*
* The application is initializing,
* the background worker required for database   synchronization is starting,
* and the I/O workers, which handle the main tasks, are being launched.
*/
void req_cache_start_work(Datum main_arg) {
    ereport(INFO, errmsg("start bg worker pg_redis_proxy pid: %d", getpid()));

    init_stats();
    ereport(INFO, errmsg("finish init stats"));

    init_def_resp();

    init_commands();
    ereport(INFO, errmsg("finish init commands"));

    init_cache_gc();
    ereport(INFO, errmsg("finish init cache gc"));

    ereport(INFO, errmsg("start init db worker"));
    init_db_worker();
    ereport(INFO, errmsg("finish init db worker %d", config.worker_conf.count_worker));

    init_workers();
}
