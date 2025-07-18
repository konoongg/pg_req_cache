#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>

#include "postgres.h"

#include "alloc.h"
#include "cache_serializer.h"
#include "invalid_trans.h"
#include "logger.h"
#include "shmem.h"

#define write_lock false
#define read_lock true

cache_invalidate* cache_inv;
extern shared_struct* shmem_data;

static void inv_lock(invalidate* inv, bool is_read_lock) {
    if (is_read_lock) {
        int err = pthread_rwlock_rdlock(inv->lock);
        if (err != 0) {
            cache_log(CACHE_ERROR, "inv_lock: pthread_rwlock_rdlock() failed: %s\n", strerror(err));
        }
    } else {
        int err = pthread_rwlock_wrlock(inv->lock);
        if (err != 0) {
            cache_log(CACHE_ERROR, "inv_lock: pthread_rwlock_rdlock() failed: %s\n", strerror(err));
        }
    }
}

static void inv_unlock(invalidate* inv) {
    int err = pthread_rwlock_unlock(inv->lock);
    if (err != 0) {
        cache_log(CACHE_ERROR, "inv_unlock: pthread_rwlock_unlock() failed: %s\n", strerror(err));
    }
}

void init_trans_pool(void) {
    shmem_data->cache_inv = cache_inv = shalloc(sizeof(cache_invalidate));
    cache_inv->trans_pool = shalloc(INVALIDATE_XID_POOL_SIZE * sizeof(invalidate));

    for (int i = 0; i < INVALIDATE_XID_POOL_SIZE; ++i) {
        int err;
        (cache_inv->trans_pool[i]).lock = shalloc(sizeof(pthread_rwlock_t));
        err = pthread_rwlock_init((cache_inv->trans_pool[i]).lock, NULL);
        if (err != 0) {
            cache_log(CACHE_ERROR, "init_inv_pool: pthread_rwlock_init %s", strerror(err));
        }
    }
}

void finish_trans_pool(void) {
    for (int i = 0; i < INVALIDATE_XID_POOL_SIZE; ++i) {
        int err = pthread_rwlock_destroy((cache_inv->trans_pool[i]).lock);
        if (err != 0) {
            cache_log(CACHE_ERROR, "finish_inv_pool: pthread_rwlock_destroy %s", strerror(err));
        }
        shfree((cache_inv->trans_pool[i]).lock);
    }

    shfree(cache_inv->trans_pool);
    shfree(cache_inv);
}

static trans_invalidate* find_trans_by_xid(invalidate* inv, size_t xid) {
    trans_invalidate* cur = inv->first;
    while (cur != NULL) {
        if (cur->xid == xid) {
            return cur;
        }
        cur = cur->next;
    }
    return NULL;
}


static void delete_trans(trans_invalidate* cur) {
    int index = cur->xid % INVALIDATE_XID_POOL_SIZE;

    trans_invalidate* next = cur->next;
    trans_invalidate* prev = cur->prev;


    if (prev) {
        cur->prev->next = next;
    } else {
        (cache_inv->trans_pool[index]).first = next;
    }

    if (next) {
        cur->next->prev = prev;
    } else {
        (cache_inv->trans_pool[index]).last = prev;
    }

    shfree(cur);
}

trans_status check_trans_status(size_t xid) {
    trans_invalidate* trans;
    int index = xid % INVALIDATE_XID_POOL_SIZE;
    invalidate* inv = &(cache_inv->trans_pool[index]);
    trans_status  status;
    inv_lock(inv, read_lock);

    trans = find_trans_by_xid(inv, xid);
    status = trans ? atomic_load(&(trans->status)) : COMMIT;

    if (status == ABORT) {
        atomic_fetch_sub(&(trans->counter), 1);
    }

    inv_unlock(inv);
    return status;
}

void add_trans_event(key_info* key_i, created_cache_respons* res, size_t xid) {
    int index =  xid % INVALIDATE_XID_POOL_SIZE;
    invalidate* inv = &(cache_inv->trans_pool[index]);
    trans_invalidate* cur_trans;
    size_t old_xid = 0;

    inv_lock(inv, read_lock);

    cur_trans = find_trans_by_xid(inv, xid) ;
    if (!cur_trans) {
        inv_unlock(inv);
        inv_lock(inv, write_lock);

        if (inv->first == NULL) {
            inv->first = inv->last = shalloc(sizeof(trans_invalidate));
            inv->first->prev = NULL;
        } else {
            inv->last->next = shalloc(sizeof(trans_invalidate));
            inv->last->next->prev = inv->last;
            inv->last = inv->last->next;
        }

        inv->last->counter = 0;
        inv->last->status = IN_PROGRES;
        inv->last->xid = xid;

        cur_trans = inv->last;


        /* it is hard to grab a lock, but it is necessary here,
        * otherwise it is possible that we make a request to the cache having a unique lock,
        * someone makes a set request,
        * takes a unique lock on the cache entry and tries to access the transaction pool.
        * We get a deadlock
        */

        inv_unlock(inv);
        inv_lock(inv, read_lock);
    }

    //the order of operations is this way because we want
    //that if the structure of the cache data contains information about a transaction,
    //then the structures describing these transactions already exist
    old_xid = invalidate_cache(key_i, res->res, res->size, xid);
    if (old_xid != -1) {
        atomic_fetch_add(&(cur_trans->counter), 1);
    }

    if (old_xid > 0) {
        if (old_xid % INVALIDATE_XID_POOL_SIZE == index) {
            trans_invalidate*  trans = find_trans_by_xid(inv, old_xid);
            trans_status status = trans ? atomic_load(&(trans->status)) : COMMIT;

            if (status == ABORT) {
                atomic_fetch_sub(&(trans->counter), 1);
            }
        } else {
            check_trans_status(old_xid);  // if status == abort, need sub transaction use counter
        }
    }
    inv_unlock(inv);
}

void process_apply(size_t xid) {
    int index;
    invalidate* inv;
    trans_invalidate* cur;

    if (!cache_inv) {
        return;
    }

    index = xid % INVALIDATE_XID_POOL_SIZE;
    inv = &(cache_inv->trans_pool[index]);

    inv_lock(inv, write_lock);

    cur = find_trans_by_xid(inv, xid);

    if (cur) {
        delete_trans(cur);
    }

    inv_unlock(inv);
}

// void process_reset(size_t xid) {
//     int index = xid % INVALIDATE_XID_POOL_SIZE;
//     invalidate* inv = &(cache_inv->xid_inv[index]);
//     trans_invalidate* cur_trans;

//     inv_lock(inv, read_lock);
//     cur_trans = find_trans_by_xid(inv, xid);
//     assert(cur_trans);


//     atomic_store(&cur_trans->status, ABORT);

//     inv_unlock(inv);;
// }
