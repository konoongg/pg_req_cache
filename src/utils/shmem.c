#include <pthread.h>
#include <string.h>

#include "postgres.h"

#include "storage/shmem.h"

#include "cache.h"
#include "logger.h"
#include "shmem.h"

extern shared_struct* shmem_data;

extern cache* c;
extern shared_allocator* allocator;
extern cache_invalidate* cache_inv;
extern db_meta_data* meta;

#define UNUSE 0

bool load_shmem_data = false;

void init_shmem(void) {
    int err = pthread_mutex_init(&(shmem_data->allocator.lock), NULL);
    if (err != 0){
       cache_log(CACHE_ERROR, "queue_init: pthread_mutex_init() failed: %s\n", strerror(err));
    }
}

void load_shared_struct(void) {
    if (load_shmem_data) {
        return;
    }

    allocator = &(shmem_data->allocator);
    c = shmem_data->c;
    cache_inv = shmem_data->cache_inv;
    meta = shmem_data->meta;
    load_shmem_data = true;
}

