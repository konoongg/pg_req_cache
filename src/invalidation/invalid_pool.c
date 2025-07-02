#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>

#include "postgres.h"

#include "utils/elog.h"

#include "alloc.h"
#include "cache_serializer.h"
#include "invalid_pool.h"
#include "logger.h"

#define write_lock false
#define read_lock true

cache_invalidate* cache_inv;


static void inv_lock(invalidate* inv, bool is_read_lock) {
    if (is_read_lock) {
        int err = pthread_rwlock_rdlock(inv->lock);
        if (err != 0) {
            ereport(INFO, errmsg("inv_lock: pthread_rwlock_rdlock() failed: %s\n", strerror(err)));
            abort();
        }
    } else {
        int err = pthread_rwlock_wrlock(inv->lock);
        if (err != 0) {
            ereport(INFO, errmsg("inv_lock: pthread_rwlock_rdlock() failed: %s\n", strerror(err)));
            abort();
        }
    }
}

static void inv_unlock(invalidate* inv) {
    int err = pthread_rwlock_unlock(inv->lock);
    if (err != 0) {
        ereport(INFO, errmsg("inv_unlock: pthread_rwlock_unlock() failed: %s\n", strerror(err)));
        abort();
    }
}

static void delete_xid(xid_invalidate* cur) {
    int index = cur->xid % INVALIDATE_XID_POOL_SIZE;
    cache_log(CACHE_DEBUG, "delete_xid: xid %ld", cur->xid);

    xid_invalidate* next = cur->next;
    xid_invalidate* prev = cur->prev;


    cache_log(CACHE_DEBUG, "delete_xid: next %p prev %p", next, prev);

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


    cache_log(CACHE_DEBUG, "delete_xid: cache_inv->xid_inv->first %p", cache_inv->xid_inv->first);

    free(cur);
}

static xid_invalidate* find_xid(invalidate* xid_inv, size_t xid) {
    cache_log(CACHE_DEBUG, "find_xid START");
    xid_invalidate* cur = xid_inv->first;

    while (cur != NULL) {
        cache_log(CACHE_DEBUG, "find_xid: xid_inv->first %p xid_inv->first->next %p xid %ld", xid_inv->first,  xid_inv->first->next, xid_inv->first->xid);
        if (cur->xid == xid) {
            return cur;
        }
        cur = cur->next;
    }
    return NULL;
}

void init_inv_pool(void) {
    cache_inv = wcalloc(sizeof(cache_invalidate));
    cache_inv->xid_inv = wcalloc(INVALIDATE_XID_POOL_SIZE * sizeof(invalidate));

    for (int i = 0; i < INVALIDATE_XID_POOL_SIZE; ++i) {
        int err;
        (cache_inv->xid_inv[i]).lock = wcalloc(sizeof(pthread_rwlock_t));
        err = pthread_rwlock_init((cache_inv->xid_inv[i]).lock, NULL);
        if (err != 0) {
            ereport(INFO, errmsg("init_inv_pool: pthread_rwlock_init %s", strerror(err)));
            abort();
        }
    }
}

void finish_inv_pool(void) {
    for (int i = 0; i < INVALIDATE_XID_POOL_SIZE; ++i) {
        int err = pthread_rwlock_destroy((cache_inv->xid_inv[i]).lock);
        if (err != 0) {
            ereport(INFO, errmsg("finish_inv_pool: pthread_rwlock_destroy %s", strerror(err)));
            abort();
        }
        free((cache_inv->xid_inv[i]).lock);
    }

    free(cache_inv->xid_inv);
    free(cache_inv);
}


bool check_inv_xid(size_t xid) {
    xid_invalidate* cur;
    int index = xid % INVALIDATE_XID_POOL_SIZE;
    invalidate* xid_inv = &(cache_inv->xid_inv[index]);

    inv_lock(xid_inv, read_lock);

    cur = find_xid(xid_inv, xid);

    inv_unlock(xid_inv);
    return cur != NULL;
}

void add_xid_event(key_info* key_i, size_t xid, ht_data* data) {
    cache_log(CACHE_DEBUG, "add_xid_event: start xid %ld", xid);

    int index = xid % INVALIDATE_XID_POOL_SIZE;
    invalidate* xid_inv = &(cache_inv->xid_inv[index]);
    xid_invalidate* last;

    if (xid_inv->first == NULL) {
        xid_inv->first = xid_inv->last = wcalloc(sizeof(xid_invalidate));
        xid_inv->first->prev = NULL;
        cache_log(CACHE_DEBUG, "add_xid_event: xid_inv->first  %p", xid_inv->first);
    } else {
        xid_inv->last->next = wcalloc(sizeof(xid_invalidate));
        xid_inv->last->next->prev = xid_inv->last;
        xid_inv->last = xid_inv->last->next;
    }
    last = xid_inv->last;

    last->next = NULL;
    last->xid = xid;

    cache_log(CACHE_DEBUG, "add_xid_event: xid_inv->last %p xid_inv->last->next %p xid %ld", xid_inv->last,  xid_inv->last->next, xid_inv->last->xid);
    cache_log(CACHE_DEBUG, "add_xid_event: xid_inv->last %p last->next %p xid %ld", xid_inv->last,  last->next, last->xid);

    atomic_store(&(data->xid_inv), xid);

    if (last->first_data == NULL) {
        last->first_data = last->last_data = data;
    } else {
        last->last_data->next_inv = data;
        last->last_data = data;
    }
}

void process_apply(size_t xid) {
    int index = xid % INVALIDATE_XID_POOL_SIZE;
    invalidate* xid_inv = &(cache_inv->xid_inv[index]);
    xid_invalidate* cur;

    inv_lock(xid_inv, write_lock);

    cur = find_xid(xid_inv, xid);

    if (cur) {
        delete_xid(cur);
    }
    inv_unlock(xid_inv);
}

void process_reset(size_t xid) {
    int index = xid % INVALIDATE_XID_POOL_SIZE;
    invalidate* xid_inv = &(cache_inv->xid_inv[index]);
    xid_invalidate* cur;
    ht_data* cur_data;

    inv_lock(xid_inv, write_lock);

    cur = find_xid(xid_inv, xid);

    if (cur) {
        /*
        * It's safe to modify these fields here because they aren't accessed elsewhere,
        * and the corresponding cache structure cannot be deleted yet. This is because
        * when we receive the event, we've already marked it as a candidate for invalidation.
        *
        * The structure remains protected from deletion at this stage—the invalidation
        * event acts as a logical lock ensuring its existence during processing.
        */

        cur_data = cur->first_data;

        while (cur_data != NULL) {
            ht_data* next = cur_data->next_inv;
            cur_data->xid_inv = 0;
            cur_data->next_inv->next_inv = NULL;
            cur_data = next;
        }

        delete_xid(cur);

    }
    inv_unlock(xid_inv);
    return;
}