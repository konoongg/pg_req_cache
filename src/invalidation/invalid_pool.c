#include <assert.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>

#include "postgres.h"

#include "utils/elog.h"

#include "alloc.h"
#include "invalid_pool.h"
#include "cache_serializer.h"

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
    xid_invalidate* next = cur->next;
    xid_invalidate* prev = cur->prev;

    if (prev) {
        cur->prev->next = next;
    }

    if (next) {
        cur->next->prev = prev;
    }

    free(cur);
}

static xid_invalidate* find_xid(invalidate* xid_inv, size_t xid) {
    xid_invalidate* cur = xid_inv->first;

    while (cur != NULL) {
        if (cur->xid == xid) {
            return cur;
        }
        cur = cur->next;
    }
    return NULL;
}

void init_inv_pool(void) {
    cache_inv = wcalloc(sizeof(cache_invalidate));
    cache_inv->xid_inv = wcalloc(INVALIDATE_XID_POOL_SIZE * sizeof(xid_invalidate));

    for (int i = 0; i < INVALIDATE_XID_POOL_SIZE; ++i) {
        cache_inv->xid_inv[i] = wcalloc(sizeof(invalidate));
        cache_inv->xid_inv[i].last = cache_inv->xid_inv[i].first = NULL;
    }
}


bool check_inv_xid(size_t xid) {
    xid_invalidate* cur;
    int index = xid % INVALIDATE_XID_POOL_SIZE;
    invalidate* xid_inv = &(cache_inv->xid_inv[xid]);

    inv_lock(xid_inv, read_lock);

    cur = find_xid(xid_inv, xid);

    inv_unlock(xid_inv);
    return cur != NULL;
}

void add_xid_event(key_info* key_i, size_t xid, ht_data* data) {
    int index = xid % INVALIDATE_XID_POOL_SIZE;
    invalidate* xid_inv = &(cache_inv->xid_inv[xid]);

    if (xid_inv->first = NULL) {
        xid_inv->first = xid_inv->last = wcalloc(sizeof(xid_invalidate));
        xid_inv->first->prev = NULL;
    } else {
        xid_inv->last->next = wcalloc(sizeof(xid_invalidate));
        xid_inv->last->next->prev = xid_inv->last;
        xid_inv->last = xid_inv->last->next;
    }
    xid_invalidate* last = xid_inv->last;

    last->next = NULL;
    last->xid = xid;

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
    invalidate* xid_inv = &(cache_inv->xid_inv[xid]);

    inv_lock(xid_inv, write_lock);

    assert(xid_inv->first);
    xid_invalidate* cur = find_xid(xid_inv, xid);

    assert(cur);

    delete_xid(cur);
    inv_unlock(xid_inv);
}

void process_reset(size_t xid) {
    int index = xid % INVALIDATE_XID_POOL_SIZE;
    invalidate* xid_inv = &(cache_inv->xid_inv[xid]);

    inv_lock(xid_inv, write_lock);

    assert(xid_inv->first);
    xid_invalidate* cur = find_xid(xid_inv, xid);

    assert(cur);

    /*
    * It's safe to modify these fields here because they aren't accessed elsewhere,
    * and the corresponding cache structure cannot be deleted yet. This is because
    * when we receive the event, we've already marked it as a candidate for invalidation.
    *
    * The structure remains protected from deletion at this stage—the invalidation
    * event acts as a logical lock ensuring its existence during processing.
    */

    ht_data* cur_data = cur->first_data;

    while (cur_data != NULL) {
        cur_data->xid_inv = 0;
        ht_data* next = cur_data->next_inv;
        cur_data->next_inv->next_inv = NULL;
        cur_data = next;
    }

    delete_xid(cur);

    inv_unlock(xid_inv);
    return;
}