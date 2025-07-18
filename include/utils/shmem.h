#ifndef REQ_CACHE_SHMEM_H
#define REQ_CACHE_SHMEM_H

#include "cache.h"
#include "invalid_trans.h"
#include "meta_db.h"

typedef struct shared_allocator shared_allocator;
typedef struct shared_struct shared_struct;

void init_shmem(void);
void load_shared_struct(void);

struct shared_allocator {
    void* mem;
    int mem_size;
    int allocated_mem;
    pthread_mutex_t lock;
};

struct shared_struct {
    cache_invalidate* cache_inv;
    cache* c;
    shared_allocator allocator;
    db_meta_data* meta;
};


#endif