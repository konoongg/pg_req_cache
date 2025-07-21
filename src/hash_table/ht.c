#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sys/time.h>

#include "postgres.h"

#include "alloc.h"
#include "config.h"
#include "ht.h"
#include "invalid_trans.h"
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

//get can only change form UNKNOWN to VALID/INVALID
static bool check_data_valid(ht_data* data) {
    invalid_status inv_status;

    if (data->xid_inv == 0) {
        return true;
    }

    inv_status = atomic_load(&(data->inv_status));
    if (inv_status == INVALID) {
        return false;
    } else if (inv_status == VALID) {
        return true;
    } else if (inv_status == UNKNOWN) {
        invalid_status new_status = check_trans_status(data->xid_inv);
        atomic_store(&(data->inv_status), new_status);
        if (new_status == INVALID) {
            return false;
        } else {
            return true;
        }
    }
    cache_log(CACHE_ERROR, "data invalidation status unlnown");

    return false;
}

static bool check_data_process_inv(ht_data* data) {

    if (data->xid_inv == 0 || data->inv_status != UNKNOWN) {
        return false;
    }

    if (check_trans_status(data->xid_inv) == IN_PROGRES) {
        return true;
    }
    return false;
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
    shfree(version);
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

    if (all_del && !check_data_process_inv(cur_data)) {
        free_data(ht, basket, cur_data, prev_data);
    }
}

static size_t process_inv(hash_table* ht, ht_data* data, size_t new_xid) {
    size_t xid = data->xid_inv;
    if (xid == 0) {
        return xid;
    } else if (xid == new_xid) {
        ht->value_free(data->inv_value->value);
        shfree(data->value_cur);
        return 0;
    }

    switch (data->inv_status) {
        case VALID:
            ht->value_free(data->inv_value->value);
            shfree(data->value_cur);
            break;
        case INVALID:
            data->value_cur->next = data->inv_value;
            data->value_cur = data->value_cur->next;
            break;
        case UNKNOWN:
            trans_status t_status = check_trans_status(xid) ;
            assert(t_status != IN_PROGRES);

            data->inv_status = t_status == COMMIT ? INVALID : VALID;
            process_inv(ht, data, new_xid);
            break;
    }

    data->xid_inv = 0;
    return xid;
}

hash_table* create_ht(create_ht_info* info) {
    hash_table* ht = shalloc(sizeof(hash_table));

    ht->count_baskets = info->count_basket;
    ht->baskets = shalloc(ht->count_baskets * sizeof(ht_basket));
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

        (ht->baskets[i]).lock = shalloc(sizeof(pthread_rwlock_t));
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
                shfree(version);
                version = next_version;
            }
            ht->free_data(cur_data);
            cur_data = new_data;
        }

        err = pthread_rwlock_destroy(basket->lock);
        if (err != 0) {
            cache_log(CACHE_ERROR, "free_cache: pthread_rwlock_destroy %s", strerror(err));
        }
        shfree((void*) basket->lock);
    }
    shfree(ht->baskets);
    shfree(ht);
}

void drop_version(data_version* version) {
    atomic_fetch_sub(&(version->usage_counter), 1);
    assert(version->usage_counter >= 0);
}

data_version* get_data(hash_table* ht, find_ht_data* find) {
    ht_basket* basket;
    ht_data* data;
    void* result = NULL;

    basket = get_basket(ht, find->hash_key, find->hash_key_size);
    basket_lock(basket, read_lock);
    data = find_data_in_basket(ht, basket, find->find_key, WITH_TLL);
    if (data != NULL) {
        if (check_data_valid(data)) {
            result = data->value_cur;
            atomic_fetch_add(&(data->value_cur->usage_counter), 1);
        } else {
            result = data->inv_value;
            atomic_fetch_add(&(data->inv_value->usage_counter), 1);
        }
    }

    basket_unlock(basket);
    return result;
}

// size_t prepare_invalidate(hash_table* ht, create_ht_data* new_data, size_t new_inv_xid) {
//     ht_basket* basket;
//     ht_data* data;
//     size_t old_xid;

//     basket = get_basket(ht, new_data->hash_key, new_data->hash_key_size);
//     basket_lock(basket, write_lock);

//     data = find_data_in_basket(ht, basket, new_data->find_key, WITH_TLL);

//     if (data == NULL) {
//         basket_unlock(basket);
//         return -1;
//     }

//     if (data->inv_value) {
//         switch (data->inv_status) {
//             case VALID:
//                 shfree(data->inv_value);
//                 break;
//             case INVALID:
//                 data->value_cur->next = data->inv_value;
//                 break;
//             case UNKNOWN:
//                 cache_log(CACHE_ERROR, "prepare_invalidate: prev transaction was not completed");
//         }
//     }

//     data->inv_value = shalloc(sizeof(data_version));

//     old_xid = data->xid_inv;
//     data->xid_inv = new_inv_xid;
//     data->inv_status = UNKNOWN;

//     basket_unlock(basket);
//     return old_xid;
// }


size_t set_invalid_data(hash_table* ht, create_ht_data* new_data, size_t xid) {

    ht_basket* basket;
    ht_data* data;
    int data_size;
    size_t old_xid;

    basket = get_basket(ht, new_data->hash_key, new_data->hash_key_size);

    basket_lock(basket, write_lock);

    data = find_data_in_basket(ht, basket, new_data->find_key, WITHOUT_TLL);
    assert(data);

    old_xid = process_inv(ht, data, xid);

    if (!data->inv_value) {
        data->inv_value = shalloc(sizeof(data_version));
    }

  

    data->inv_value->value = new_data->value;
    data->inv_value->next = NULL;
    data->inv_value->usage_counter = 0;
    data->expire_ms = new_data->expire_ms;
    data_size = new_data->find_key_size + new_data->value_size + sizeof(ht_data);
    data->inv_status = UNKNOWN;
    data->xid_inv = xid;

    atomic_fetch_add(&(ht->cur_ht_size), data_size);

    data->last_time = time(NULL);
    if (data->last_time == -1) {
        cache_log(CACHE_ERROR, "set_data: time error  %s", strerror(errno));
    }

    basket_unlock(basket);
    return old_xid;
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
            data = basket->first = basket->last = shalloc(sizeof(ht_data));
        } else {
            basket->last->next = shalloc(sizeof(ht_data));
            data = basket->last = basket->last->next;
        }
        data->next = NULL;
        data->find_key = new_data->find_key;
        data->value_first = data->value_cur = shalloc(sizeof(data_version));
        data->value_cur->dirty = false;
    } else {
        size_t old_xid = process_inv(ht, data, 0);
        if (old_xid > 0) {
            check_trans_status(old_xid); // if status == abort, need sub transaction use counter
        }

        if (data->value_cur->usage_counter == 0) {
            atomic_fetch_sub(&(ht->cur_ht_size), data->ht_data_size);
            ht->value_free(data->value_cur->value);
        } else {
            data->value_cur->dirty = true;
            data->value_cur->next = shalloc(sizeof(data_version));
            data->value_cur = data->value_cur->next;
        }
    }


    data->value_cur->value = new_data->value;
    data->value_cur->next = NULL;
    data->value_cur->usage_counter = 0;
    data->expire_ms = new_data->expire_ms;
    data_size = new_data->find_key_size + new_data->value_size + sizeof(ht_data);
    data->inv_status = VALID;
    data->xid_inv = 0;

    atomic_fetch_add(&(ht->cur_ht_size), data_size);

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
            data = basket->first = basket->last = shalloc(sizeof(ht_data));
        } else {
            basket->last->next = shalloc(sizeof(ht_data));
            data = basket->last = basket->last->next;
        }
        data->next = NULL;
        data->find_key = new_data->find_key;

        data->value_first = data->value_cur = shalloc(sizeof(data_version));
        data->value_cur->dirty = false;
        data->value_cur->value = new_data->value;
        data->value_cur->next = NULL;
        data->value_cur->usage_counter = 0;
        data->xid_inv = 0;
        data->inv_status = VALID;

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
            bool all_del = true;
            data_version* version = cur_data->value_first;
            data_version* prev_version = NULL;

            bool delete_all_version = need_delete_all_version(cur_data, recomendate_ttl_s);

            while (version != NULL) {
                data_version* next_version = version->next;
                if (version->usage_counter == 0) {
                    if (version->dirty || delete_all_version) {
                        free_version(ht, cur_data, version, prev_version);
                    }

                } else {
                    if (delete_all_version ) {
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
