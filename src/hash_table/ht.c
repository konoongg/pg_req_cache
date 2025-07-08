#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sys/time.h>

#include "postgres.h"

#include "alloc.h"
#include "config.h"
#include "ht.h"
#include "invalid_pool.h"
#include "logger.h"

#define write_lock false
#define read_lock true

#define WITHOUT_TLL false
#define WITH_TLL true

extern config_cache config;

static void basket_lock(ht_basket* basket, bool is_read_lock) {
    if (is_read_lock) {
        int err = pthread_rwlock_rdlock(basket->lock);
        if (err != 0) {
            cache_log(CACHE_ERROR, "basket_lock: pthread_rwlock_rdlock() failed: %s\n", strerror(err));
        }
    } else {
        int err = pthread_rwlock_wrlock(basket->lock);
        if (err != 0) {
            cache_log(CACHE_ERROR, "basket_lock: pthread_rwlock_rdlock() failed: %s\n", strerror(err));
        }
    }
}

static void basket_unlock(ht_basket* basket) {
    int err = pthread_rwlock_unlock(basket->lock);
    if (err != 0) {
        cache_log(CACHE_ERROR, "basket_lock: pthread_rwlock_unlock() failed: %s\n", strerror(err));
    }
}


//A function to retrieve the corresponding ht bucket based on a string
static ht_basket* get_basket(hash_table* ht, char* key, int key_size) {
    u_int64_t hash;
    hash = ht->hash_func(key, key_size, &(ht->count_baskets));
    return &(ht->baskets[hash]);
}

static uint64_t get_current_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)(tv.tv_sec) * 1000 + (uint64_t)(tv.tv_usec) / 1000;
}

/*
* The function checks whether the specified bucket contains data with the provided key.
* If the data exists, a reference to it is returned; otherwise, NULL is returned.

* It might seem that invalidation slows down get requests because we need to check
* whether a transaction exists, which requires taking an rw lock. In reality, this isn't
* entirely true. If no one is trying to modify the record, then xid = 0 and no locks
* are acquired.
*
* The situation is slightly different for replication. It's assumed that writes cannot
* be performed on a replica—only reads are allowed. Instead of set/del commands,
* invalidation is used. Thus, the rw lock in the hash table is always successfully
* acquired during a get, but we might stall when trying to lock the invalidation table.
*
* This case is similar to the usual lock contention between set and get operations
* on the same key.
*/
static ht_data* find_data_in_basket(hash_table* ht, ht_basket* basket, void* find_key, bool take_tll) {
    ht_data* data;
    data = basket->first;
    while (data != NULL) {
        if (ht->cmp_key(data->find_key, find_key)) {

            if (take_tll && check_invalidate(data)) {
                bool expected_not_inv = false;
                if (atomic_compare_exchange_strong(&(data->invalidated), &expected_not_inv, true)) {
                    atomic_store(&(data->xid_inv), 0);
                }
                return NULL;
            }

            if (data->expire_ms > 0 && take_tll) {
                size_t current_ms = get_current_ms();
                size_t elapsed_ms = current_ms - (size_t)(data->last_time * 1000);
                if (elapsed_ms >= data->expire_ms ) {
                    return NULL;
                }
            } else if (data->expire_ms == 0 && ht->ttl_s  > 0 && take_tll) {
                size_t current_ms = get_current_ms();
                size_t elapsed_ms = current_ms - (size_t)(data->last_time * 1000);
                if (elapsed_ms >= ht->ttl_s  * 1000) {
                    return NULL;
                }
            }
            return data;
        }
        data = data->next;
    }
    return NULL;
}

/*
 * Verify the xid (transaction ID) which can be 0 in two distinct cases:
 *
 * 1) No transaction has attempted to modify this record yet (initial state)
 * 2) A transaction has explicitly invalidated the record by:
 *    a) First setting the invalidation flag
 *    b) Then zeroing out the xid
 *
 * Important synchronization guarantee:
 * The invalidation flag is always set BEFORE clearing the xid, therefore:
 * - If xid is 0 and flag is set → record was explicitly invalidated
 * - If xid is 0 and flag is not set → record is pristine/unmodified
 *
 * There cannot be a state where:
 * - xid is already 0 (cleared)
 * - but invalidation flag isn't set yet
 * This ordering is crucial for correct concurrency control.
 */
bool check_invalidate(ht_data* data) {
    size_t xid  = atomic_load(&(data->xid_inv));

    if (xid == 0) {
        if (atomic_load(&(data->invalidated))) {
            return true;
        }
        return false;
    }

    if (atomic_load(&(data->invalidated))) {
        return true;
    }


    if (!check_inv_xid(xid)) {
        /*
        * This check handles the race condition where:
        * 1. We read the xid
        * 2. Immediately after, the transaction aborts and removes all records
        *
        * In this case:
        * - Since all records were deleted, we won't find the transaction info
        * - This could mean either:
        *   a) The transaction committed successfully, OR
        *   b) It was aborted
        *
        * The xid=0 check resolves this ambiguity:
        * - If xid was reset to 0: Confirms the transaction was aborted
        * - Otherwise: Consider it committed (safe default)
        *
        * This ensures we never return invalidated data while maintaining good performance
        * in the common case (no ongoing invalidations).
        */

        xid = atomic_load(&(data->xid_inv));
        if (xid == 0) {
            return false;
        }

        return true;
    }
    return false;
}

static void free_version(hash_table* ht, ht_data* cur_data, data_version* version, data_version* prev_version) {
    ht->value_free(version->value);
    if (version == cur_data->value_first) {
        cur_data->value_first = version->next;
    } else {
        prev_version->next = version->next;
    }

    if (version->next == NULL) {
        cur_data->value_cur = prev_version;
    }
    free(version);
}

static void free_data(hash_table* ht, ht_basket* basket, ht_data* cur_data, ht_data* prev_data) {
    if (cur_data == basket->first) {
        basket->first = cur_data->next;
    } else {
        prev_data->next = cur_data->next;
    }

    if (cur_data->next == NULL) {
        basket->last = prev_data;
    }

    atomic_fetch_sub(&(ht->cur_ht_size), cur_data->ht_data_size);
    ht->free_data(cur_data);
}

static void free_data_from_ht(hash_table* ht, ht_basket* basket, ht_data* cur_data, ht_data* prev_data) {
    bool all_del = true;
    data_version* version = cur_data->value_first;
    data_version* prev_version = NULL;

    while (version != NULL) {
        data_version* next_version = version->next;
        if (version->usage_counter == 0) {
            free_version(ht, cur_data, version, prev_version);
        } else {
            version->dirty = true;
            prev_version = version;
            all_del = false;
        }
        version = next_version;
    }



    /*
    * We avoid deleting data here if any invalidation info exists,
    * because we don't want to handle pointer reassignment for
    * invalidated structures ourselves.
    *
    * Special case for DEL commands arriving via cache:
    * - May occur when the transaction has already completed
    * - But we haven't yet processed the WAL notification
    */

    if (all_del && atomic_load(&(cur_data->xid_inv)) == 0 && atomic_load(&(cur_data->invalidated)) == false) {
        free_data(ht, basket, cur_data, prev_data);
    }
}

hash_table* create_ht(create_ht_info* info) {
    hash_table* ht = wcalloc(sizeof(hash_table));

    ht->count_baskets = info->count_basket;
    ht->baskets = wcalloc(ht->count_baskets * sizeof(ht_basket));
    ht->ttl_s = info->ttl_s;
    ht->max_ht_size = info->max_ht_size;

    ht->hash_func = info->hash_func;
    ht->cmp_key = info->cmp_key;
    ht->copy = info->copy;
    ht->free_data = info->free_data;
    ht->value_free = info->value_free;

    atomic_store(&(ht->cur_ht_size), 0);

    for (int i = 0; i < ht->count_baskets ; ++i) {
        int err;

        (ht->baskets[i]).lock = wcalloc(sizeof(pthread_rwlock_t));
        err = pthread_rwlock_init((ht->baskets[i]).lock, NULL);
        if (err != 0) {
            cache_log(CACHE_ERROR, "create_ht: pthread_rwlock_init %s", strerror(err));
        }
    }
    return ht;
}

void destroy_ht(hash_table* ht) {
    for (int i = 0; i < ht->count_baskets; ++i) {
        int err;
        ht_basket* basket = &(ht->baskets[i]);
        ht_data* cur_data = basket->first;
        while (cur_data != NULL) {
            ht_data* new_data = cur_data->next;

            data_version* version = cur_data->value_first;

            while (version != NULL) {
                data_version* next_version = version->next;
                assert(version->usage_counter == 0);
                ht->value_free(version->value);
                free(version);
                version = next_version;
            }
            ht->free_data(cur_data);
            cur_data = new_data;
        }

        err = pthread_rwlock_destroy(basket->lock);
        if (err != 0) {
            cache_log(CACHE_ERROR, "free_cache: pthread_rwlock_destroy %s", strerror(err));
        }
        free((void*) basket->lock);
    }
    free(ht->baskets);
    free(ht);
}


void drop_version(data_version* version) {
    atomic_fetch_sub(&(version->usage_counter), 1);
    assert(version->usage_counter >= 0);
}

ht_data* prepare_invalidate(hash_table* ht, find_ht_data* find, size_t xid) {
    ht_basket* basket;
    ht_data* data;
    basket = get_basket(ht, find->hash_key, find->hash_key_size);
    basket_lock(basket, write_lock);

    data = find_data_in_basket(ht, basket, find->find_key, WITH_TLL);

    if (data != NULL) {
        data->xid_inv = xid;
    }

    basket_unlock(basket);
    return data;
}

data_version* get_data(hash_table* ht, find_ht_data* find) {
    ht_basket* basket;
    ht_data* data;
    void* result = NULL;

    basket = get_basket(ht, find->hash_key, find->hash_key_size);
    basket_lock(basket, read_lock);
    data = find_data_in_basket(ht, basket, find->find_key, WITH_TLL);
    if (data != NULL) {
        result = data->value_cur;
        atomic_fetch_add(&(data->value_cur->usage_counter), 1);
    }

    basket_unlock(basket);
    return result;
}

void set_data(hash_table* ht, create_ht_data* new_data) {
    ht_basket* basket;
    ht_data* data;
    int data_size;

    basket = get_basket(ht, new_data->hash_key, new_data->hash_key_size);

    basket_lock(basket, write_lock);

    data = find_data_in_basket(ht, basket, new_data->find_key, WITHOUT_TLL);
    if (data == NULL) {
        if (basket->first == NULL) {
            data = basket->first = basket->last = wcalloc(sizeof(ht_data));
        } else {
            basket->last->next = wcalloc(sizeof(ht_data));
            data = basket->last = basket->last->next;
        }
        data->next = NULL;
        data->find_key = new_data->find_key;
        data->value_first = data->value_cur = wcalloc(sizeof(data_version));
        data->value_cur->dirty = false;
        data->xid_inv = 0;
        data->next_inv = NULL;
    } else {
        if (data->value_cur->usage_counter == 0) {
            atomic_fetch_sub(&(ht->cur_ht_size), data->ht_data_size);
            ht->value_free(data->value_cur->value);
            data->value_cur->dirty = true;
        } else {
            data->value_cur->dirty = true;
            data->value_cur->next = wcalloc(sizeof(data_version));
            data->value_cur = data->value_cur->next;
        }
    }

    data->value_cur->value = new_data->value;
    data->value_cur->next = NULL;
    data->value_cur->usage_counter = 0;
    data->expire_ms = new_data->expire_ms;
    data_size = new_data->find_key_size + new_data->value_size + sizeof(ht_data);

    atomic_fetch_add(&(ht->cur_ht_size), data_size);

    data->invalidated = false;

    data->last_time = time(NULL);
    if (data->last_time == -1) {
        cache_log(CACHE_ERROR, "set_data: time error  %s", strerror(errno));
    }

    basket_unlock(basket);
}

void set_data_if_not_exist(hash_table* ht, create_ht_data* new_data) {
    ht_basket* basket;
    ht_data* data;

    basket = get_basket(ht, new_data->hash_key, new_data->hash_key_size);

    basket_lock(basket, write_lock);

    data = find_data_in_basket(ht, basket, new_data->find_key, WITHOUT_TLL);
    if (data == NULL) {
        if (basket->first == NULL) {
            data = basket->first = basket->last = wcalloc(sizeof(ht_data));
        } else {
            basket->last->next = wcalloc(sizeof(ht_data));
            data = basket->last = basket->last->next;
        }
        data->next = NULL;
        data->find_key = new_data->find_key;

        data->value_first = data->value_cur = wcalloc(sizeof(data_version));
        data->value_cur->dirty = false;
        data->value_cur->value = new_data->value;
        data->value_cur->next = NULL;
        data->value_cur->usage_counter = 0;

        atomic_fetch_add(&(ht->cur_ht_size), new_data->find_key_size + new_data->value_size + sizeof(ht_data));
        data->last_time = time(NULL);
        if (data->last_time == -1) {
            cache_log(CACHE_ERROR, "set_data_if_not_exist: time error  %s", strerror(errno));
        }
    } else {
        ht->value_free(new_data);
    }
    basket_unlock(basket);
}

int delete_data(hash_table* ht, find_ht_data* find) {
    ht_basket* basket;
    ht_data* data;
    ht_data* prev_data;

    basket = get_basket(ht, find->hash_key, find->hash_key_size);

    basket_lock(basket, write_lock);

    data = basket->first;
    prev_data = NULL;
    while (data != NULL) {
        if (ht->cmp_key(data->find_key, find->find_key)) {
            break;
        }
        prev_data = data;
        data = data->next;
    }

    if (data == NULL) {
        basket_unlock(basket);
        return 0;
    }
    data->value_cur->dirty = true;
    free_data_from_ht(ht, basket, data, prev_data);
    basket_unlock(basket);

    return 1;
}

size_t get_cur_size(hash_table* ht) {
    return atomic_load(&(ht->cur_ht_size));
}

static bool need_delete_all_version (ht_data* cur_data, int recomendate_ttl_s) {
    if (cur_data->expire_ms > 0) {
        size_t current_ms = get_current_ms();
        size_t elapsed_ms = current_ms - (size_t)(cur_data->last_time * 1000);
        if (elapsed_ms >= cur_data->expire_ms) {
            return true;
        }
    } else if (cur_data->expire_ms == 0 && recomendate_ttl_s > 0) {
        size_t current_ms = get_current_ms();
        size_t elapsed_ms = current_ms - (size_t)(cur_data->last_time * 1000);
        if (elapsed_ms >= recomendate_ttl_s * 1000) {
           return true;
        }
    }
    return false;
}


void ht_clean(hash_table* ht, int recomendate_ttl_s) {
    for (int i = 0 ; i < ht->count_baskets; ++i) {
        ht_basket* basket = &(ht->baskets[i]);
        ht_data* cur_data;
        ht_data* prev_data;

        basket_lock(basket, write_lock);

        cur_data = basket->first;
        prev_data = NULL;
        while (cur_data != NULL) {
            bool invalidated = cur_data->invalidated;
            bool all_del = true;
            bool delete_all_version = false;
            data_version* version = cur_data->value_first;
            data_version* prev_version = NULL;

            delete_all_version = need_delete_all_version(cur_data, recomendate_ttl_s);

            while (version != NULL) {
                data_version* next_version = version->next;
                if (version->usage_counter == 0) {
                    if (version->dirty || delete_all_version || invalidated) {
                        free_version(ht, cur_data, version, prev_version);
                    }

                } else {
                    if (delete_all_version || invalidated) {
                        version->dirty = true;
                    }
                    prev_version = version;
                    all_del = false;
                }
                version = next_version;
            }

            if (all_del) {
                free_data(ht, basket, cur_data, prev_data);
            }

            prev_data = cur_data;
            cur_data = cur_data->next;
        }
        basket_unlock(basket);
    }
}
