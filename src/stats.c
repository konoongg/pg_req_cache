#include <stdatomic.h>

#include "alloc.h"
#include "config.h"
#include "stats.h"


extern config_cache config;
extern statistics stats;

void init_stats(void) {
    stats.worker_notify = wcalloc(config.worker_conf.count_worker * sizeof(int));
    atomic_init(&(stats.cache_miss), 0);
}

void report_cache_miss(void) {
    atomic_fetch_add(&(stats.cache_miss), 1);
}

void report_worker_notify(int worker_id) {
    stats.worker_notify[worker_id] += 1;
}