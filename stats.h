#ifndef STAT_H
#define STAT_H

#include <stdatomic.h>

typedef struct statistics statistics;

void init_stats(void);
void report_cache_miss(void);
void report_worker_notify(int worker_id);

struct statistics {
    _Atomic int cache_miss;
    int* worker_notify;
};

#endif