#ifndef CACHE_GC_H
#define CACHE_GC_H

#include "connection.h"

typedef struct cache_gc cache_gc;

void wake_up_cache_gc(void);
void init_cache_gc(void);

struct cache_gc {
    wthread* gc_wthrd;
    pthread_mutex_t* not_lock; // notify lock
};

#endif