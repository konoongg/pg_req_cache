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
    cache_inv = shalloc(sizeof(cache_invalidate));
    cache_inv->xid_inv = shcalloc(INVALIDATE_XID_POOL_SIZE * sizeof(invalidate));

    for (int i = 0; i < INVALIDATE_XID_POOL_SIZE; ++i) {
        int err;
        (cache_inv->xid_inv[i]).lock = shcalloc(sizeof(pthread_rwlock_t));
        err = pthread_rwlock_init((cache_inv->xid_inv[i]).lock, NULL);
        if (err != 0) {
            cache_log(CACHE_ERROR, "init_inv_pool: pthread_rwlock_init %s", strerror(err));
        }
    }
}

void finish_trans_pool(void) {
    for (int i = 0; i < INVALIDATE_XID_POOL_SIZE; ++i) {
        int err = pthread_rwlock_destroy((cache_inv->xid_inv[i]).lock);
        if (err != 0) {
            cache_log(CACHE_ERROR, "finish_inv_pool: pthread_rwlock_destroy %s", strerror(err));
        }
        shfree((cache_inv->xid_inv[i]).lock);
    }

    shfree(cache_inv->xid_inv);
    shfree(cache_inv);
}

static trans_invalidate* find_trans_by_xid(invalidate* xid_inv, size_t xid) {
    trans_invalidate* cur = xid_inv->first;
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
        (cache_inv->xid_inv[index]).first = next;
    }

    if (next) {
        cur->next->prev = prev;
    } else {
        (cache_inv->xid_inv[index]).last = prev;
    }

    free(cur);
}


trans_status check_trans_status(size_t xid) {
    trans_invalidate* trans;
    int index = xid % INVALIDATE_XID_POOL_SIZE;
    invalidate* xid_inv = &(cache_inv->xid_inv[index]);
    trans_status  status;
    inv_lock(xid_inv, read_lock);

    trans = find_xid(xid_inv, xid);

    assert(trans);

    inv_unlock(xid_inv);
    return atomic_load(&(trans->status));
}

void add_trans_event(key_info* key_i, created_cache_respons* res, size_t xid) {
    size_t expeted_prepare = 0;
    int index =  xid % INVALIDATE_XID_POOL_SIZE;
    invalidate* inv = = &(cache_inv->trans_pool[index]);
    trans_invalidate* cur_trans;
    bool this_key_exist;

    inv_lock(inv, read_lock);

    this_key_exist = prepare_invalid_cache(key_i, res->res, res->size, xid);

    if (!this_key_exist) {
        inv_unlock(inv);
        return;
    }

    cur_trans = find_trans_by_xid(inv, xid) ;
    if (!cur_trans) {
        inv_unlock(inv);
        inv_lock(inv, write_lock);

        if (xid_inv->first == NULL) {
            xid_inv->first = xid_inv->last = shcalloc(sizeof(trans_invalidate));
            xid_inv->first->prev = NULL;
        } else {
            xid_inv->last->next = shalloc(sizeof(trans_invalidate));
            xid_inv->last->next->prev = xid_inv->last;
            xid_inv->last = xid_inv->last->next;
        }

        xid_inv->last->counter = 0;
        xid_inv->last->status = IN_PROGRES;
        xid_inv->last->xid = xid;

        cur_trans = xid_inv->last;
    }


    atomic_fetch_add(&(cur_trans->counter), 1);
    inv_unlock(inv);
}

void process_apply(size_t xid) {
    int index = xid % INVALIDATE_XID_POOL_SIZE;
    invalidate* xid_inv = &(cache_inv->xid_inv[index]);
    trans_invalidate* cur;

    inv_lock(xid_inv, write_lock);

    cur = find_trans_by_xid(xid_inv, xid);

    if (cur) {
        delete_trans(cur);
    }

    inv_unlock(xid_inv);
}

void process_reset(size_t xid) {
    int index = xid % INVALIDATE_XID_POOL_SIZE;
    invalidate* inv = &(cache_inv->xid_inv[index]);
    trans_invalidate* cur_trans;

    inv_lock(inv, read_lock);
    cur_trans = find_trans_by_xid(inv, xid);
    assert(cur_trans);


    atomic_store(&cur_trans->status, ABORT);

    inv_unlock(inv);;
}
