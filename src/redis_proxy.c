#include <stdlib.h>
#include <unistd.h>

#include "postgres.h"

#include "executor/executor.h"
#include "access/xact.h"
#include "fmgr.h"
#include "miscadmin.h"
#include "postmaster/bgworker.h"
#include "storage/ipc.h"
#include "storage/lwlock.h"
#include "storage/shmem.h"

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

struct shared_struct {
    cache* c;
    shared_allocator* allocator;
    
};

extern cache* c;

static shmem_request_hook_type prev_shmem_request_hook = NULL;
static shmem_startup_hook_type prev_shmem_startup_hook = NULL;
static ExecutorFinish_hook_type prev_ExecutorFinish = NULL;

void _PG_init(void) {
    register_proxy();
}

static void req_cache_shmem_request(void) {
    init_config();

    if (prev_shmem_request_hook) {
		prev_shmem_request_hook();
    }

	RequestAddinShmemSpace(sizeof(cache) + config.c_conf.max_storage_size);
}

static void req_cache_shmem_startup(void) {
    bool found;

    if (prev_shmem_startup_hook) {
		prev_shmem_startup_hook();
    }

    //this hook is called before postmaster starts accepting connections, so no blocking is needed
    c = ShmemInitStruct("pg_req_cache", sizeof(cache) + config.c_conf.max_storage_size, &found);

    if (found) {
        cache_log(CACHE_WARNING, "pg_req_cache already exist");
    }
}

static void req_cache_ExecutorFinish(QueryDesc* queryDesc) {

    if (prev_ExecutorFinish) {
    	prev_ExecutorFinish(queryDesc);
    } else {
    	standard_ExecutorFinish(queryDesc);
    }
    inv_process_command(queryDesc);
}

static void req_cache_xact_cb(XactEvent event, void *arg) {
    inv_process_xact(event, arg);
}

static void register_proxy(void) {
    BackgroundWorker worker;

    prev_shmem_request_hook = shmem_request_hook;
    shmem_request_hook = req_cache_shmem_request;

    prev_shmem_startup_hook = shmem_startup_hook;
	shmem_startup_hook = req_cache_shmem_startup;
    memset(&worker, 0, sizeof(BackgroundWorker));
    worker.bgw_flags = BGWORKER_SHMEM_ACCESS;
    worker.bgw_start_time =  BgWorkerStart_ConsistentState;
    strncpy(worker.bgw_library_name, "pg_redis_proxy", 15);
    strncpy(worker.bgw_function_name, "proxy_start_work", 17);
    strncpy(worker.bgw_name, "pg_redis_proxy", 15);
    strncpy(worker.bgw_type, "redis proxy server", 19);
    worker.bgw_restart_time = BGW_NEVER_RESTART;
    RegisterBackgroundWorker(&worker);

    prev_ExecutorFinish = ExecutorFinish_hook;
	ExecutorFinish_hook = req_cache_ExecutorFinish;

    RegisterXactCallback(req_cache_xact_cb, NULL);
}

/*
* The application is initializing,
* the background worker required for database synchronization is starting,
* and the I/O workers, which handle the main tasks, are being launched.
*/
void proxy_start_work(Datum main_arg) {
    cache_log(CACHE_INFO, "start bg worker pg_redis_proxy pid");

    init_shared_allocator(c + sizeof(cache), config.c_conf.max_storage_size);
    cache_log(CACHE_INFO, "finish init allocator");

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
    init_workers();
}
