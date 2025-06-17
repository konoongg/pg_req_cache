#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sys/time.h>

#include "postgres.h"
#include "utils/elog.h"

#include "alloc.h"
#include "config.h"
#include "ht.h"

#define write_lock false
#define read_lock true

#define WITHOUT_TLL false
#define WITH_TLL true


extern config_redis config;

bool need_delete_all_version (ht_data* cur_data, int recomendate_ttl_s);
static ht_basket* get_basket(hash_table* ht, char* key, int key_size);
static ht_data* find_data_in_basket(hash_table* ht, ht_basket* basket, void* find_key, bool take_tll);
static uint64_t get_current_ms();
static void basket_lock(ht_basket* basket, bool is_read_lock);
static void basket_unlock(ht_basket* basket);
static void free_data_from_ht(hash_table* ht, ht_basket* basket, ht_data* cur_data, ht_data* prev_data);
void free_data(hash_table* ht, ht_basket* basket, ht_data* cur_data, ht_data* prev_data);
void free_version(hash_table* ht, ht_data* cur_data, data_version* version, data_version* prev_version);

void basket_lock(ht_basket* basket, bool is_read_lock) {
    if (is_read_lock) {
        int err = pthread_rwlock_rdlock(basket->lock);
        if (err != 0) {
            ////ereport(INFO, errmsg("basket_lock: pthread_rwlock_rdlock() failed: %s\n", strerror(err)));
            abort();
        }
    } else {
        int err = pthread_rwlock_wrlock(basket->lock);
        if (err != 0) {
            ////ereport(INFO, errmsg("basket_lock: pthread_rwlock_rdlock() failed: %s\n", strerror(err)));
            abort();
        }
    }
}

void basket_unlock(ht_basket* basket) {
    int err = pthread_rwlock_unlock(basket->lock);
    if (err != 0) {
        ////ereport(INFO, errmsg("basket_lock: pthread_rwlock_unlock() failed: %s\n", strerror(err)));
        abort();
    }
}


//A function to retrieve the corresponding ht bucket based on a string.
ht_basket* get_basket(hash_table* ht, char* key, int key_size) {
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
*/
ht_data* find_data_in_basket(hash_table* ht, ht_basket* basket, void* find_key, bool take_tll) {
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
                if (elapsed_ms >= ht->ttl_s  * 1000 ) {
                    return NULL;
                }
            }
            return data;
        }
        data = data->next;
    }
    return NULL;
}

void free_version(hash_table* ht, ht_data* cur_data, data_version* version, data_version* prev_version) {
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

void free_data(hash_table* ht, ht_basket* basket, ht_data* cur_data, ht_data* prev_data) {
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

void free_data_from_ht(hash_table* ht, ht_basket* basket, ht_data* cur_data, ht_data* prev_data) {
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

    if (all_del) {
        free_data(ht, basket, cur_data, prev_data);
    }
}

hash_table* create_ht(create_ht_info* info) {
    hash_table* ht = shalloc(sizeof(hash_table));
    ereport(INFO, errmsg("create_ht: first shalloc suc"));

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
            ////ereport(INFO, errmsg("create_ht: pthread_rwlock_init %s", strerror(err)));
            abort();
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
            ////ereport(INFO, errmsg("free_cache: pthread_rwlock_destroy %s", strerror(err)));
            abort();
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
        if (data->value_cur->usage_counter == 0) {
            atomic_fetch_sub(&(ht->cur_ht_size), data->ht_data_size);
            ht->value_free(data->value_cur->value);
            data->value_cur->dirty = true;
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

    atomic_fetch_add(&(ht->cur_ht_size), data_size);

    data->last_time = time(NULL);
    if (data->last_time == -1) {
        char* err = strerror(errno);
        ereport(INFO, errmsg("set_cache: time error  %s", err));
        abort();
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

        atomic_fetch_add(&(ht->cur_ht_size), new_data->find_key_size + new_data->value_size + sizeof(ht_data));
        data->last_time = time(NULL);
        if (data->last_time == -1) {
            char* err = strerror(errno);
            ereport(INFO, errmsg("set_cache: time error  %s", err));
            abort();
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

bool need_delete_all_version (ht_data* cur_data, int recomendate_ttl_s) {
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
            bool delete_all_version = false;
            data_version* version = cur_data->value_first;
            data_version* prev_version = NULL;

            delete_all_version = need_delete_all_version(cur_data, recomendate_ttl_s);

            while (version != NULL) {
                data_version* next_version = version->next;
                if (version->usage_counter == 0) {
                    if (version->dirty || delete_all_version) {
                        free_version(ht, cur_data, version, prev_version);
                    }

                } else {
                    if (delete_all_version) {
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