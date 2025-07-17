#ifndef REQ_CACHE_SHMEM_H
#define REQ_CACHE_SHMEM_H

#include "cache.h"
#include "invalid_trans.h"

typedef struct shared_allocator shared_allocator;
typedef struct shared_struct shared_struct;

void init_shmem(void);

struct shared_allocator {
    void* mem;
    int mem_size;
    int allocated_mem;
    pthread_mutex_t lock;
};

struct shared_struct {
    cache_invalidate* invalidator;
    cache* c;
    shared_allocator allocator;
};


#endif