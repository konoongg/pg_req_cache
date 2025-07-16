#include <pthread.h>

#include "logger.h"
#include "shmem.h"

extern shared_struct* shmem_data;

void init_shmem() {
    int err = pthread_mutex_init(&(shmem_data->allocator.lock), NULL);
    if (err != 0){
       cache_log(CACHE_ERROR, "queue_init: pthread_mutex_init() failed: %s\n", strerror(err));
    }
}