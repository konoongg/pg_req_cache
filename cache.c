#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "postgres.h"
#include "utils/elog.h"

#include "alloc.h"
#include "cache_gc.h"
#include "cache.h"
#include "config.h"
#include "hash.h"
#include "storage_data.h"

bool check_ttl(cache_basket* basket, cache_data* prev_data, cache_data* data);
cache_basket* get_basket(char* key, int key_size);
cache_data* find_data_in_basket(cache_basket* basket, char* key, int key_size);
cache_need_gc add_cache_size(size_t size);
void basket_lock(cache_basket* basket, bool is_read_lock);
void basket_unlock(cache_basket* basket);
void free_data_from_cache(cache_data* data);
void free_storage(kv_storage storage);
void sub_cache_size(size_t size);

cache* c;
extern config_redis config;

cache_need_gc add_cache_size(size_t size) {
    cache_need_gc status = HAVE_SIZE;
    int err = pthread_spin_lock(c->size_lock);
    if (err != 0) {
        ereport(INFO, errmsg("get_cur_cache_size: pthread_spin_lock %s", strerror(err)));
        abort();
    }
    c->cur_cache_size += size;
    if (c->cur_cache_size >= config.c_conf.max_storage_size) {
        status = NEED_GC;
    }
    err = pthread_spin_unlock(c->size_lock);
    if (err != 0) {
        ereport(INFO, errmsg("get_cur_cache_size: pthread_spin_unlock %s", strerror(err)));
        abort();
    }
    return status;
}

void sub_cache_size(size_t size) {
    int err = pthread_spin_lock(c->size_lock);
    if (err != 0) {
        ereport(INFO, errmsg("get_cur_cache_size: pthread_spin_lock %s", strerror(err)));
        abort();
    }
    c->cur_cache_size -= size;
    assert(c->cur_cache_size >= 0);
    err = pthread_spin_unlock(c->size_lock);
    if (err != 0) {
        ereport(INFO, errmsg("get_cur_cache_size: pthread_spin_unlock %s", strerror(err)));
        abort();
    }
}

void basket_lock(cache_basket* basket, bool is_read_lock) {
    if (is_read_lock) {
        int err = pthread_rwlock_rdlock(basket->lock);
        if (err != 0) {
            ereport(INFO, errmsg("basket_lock: pthread_rwlock_rdlock() failed: %s\n", strerror(err)));
            abort();
        }
    } else {
        int err = pthread_rwlock_wrlock(basket->lock);
        if (err != 0) {
            ereport(INFO, errmsg("basket_lock: pthread_rwlock_rdlock() failed: %s\n", strerror(err)));
            abort();
        }
    }
}

void basket_unlock(cache_basket* basket) {
    int err = pthread_rwlock_unlock(basket->lock);
    if (err != 0) {
        ereport(INFO, errmsg("basket_lock: pthread_rwlock_unlock() failed: %s\n", strerror(err)));
        abort();
    }
}


// It releases the data from the cache.
void free_data_from_cache(cache_data* data) {
    sub_cache_size(data->cache_data_size);
    free_values(data->v);
    free(data->key);
    free(data);
}

size_t get_cur_cache_size(void) {
    size_t size;
    int err;

    err = pthread_spin_lock(c->size_lock);
    if (err != 0) {
        ereport(INFO, errmsg("get_cur_cache_size: pthread_spin_lock %s", strerror(err)));
        abort();
    }
    size = c->cur_cache_size;
    err = pthread_spin_unlock(c->size_lock);
    if (err != 0) {
        ereport(INFO, errmsg("get_cur_cache_size: pthread_spin_unlock %s", strerror(err)));
        abort();
    }
    return size;
}

void init_cache(void) {
    kv_storage* storage;
    int err;

    c = wcalloc(sizeof(cache));
    c->storage = wcalloc(sizeof(kv_storage));
    c->count_basket = config.c_conf.count_basket;
    c->cur_cache_size = 0;
    c->size_lock = wcalloc(sizeof(pthread_spinlock_t));
    err = pthread_spin_init(c->size_lock, PTHREAD_PROCESS_PRIVATE);
    if (err != 0) {
        ereport(INFO, errmsg("init_cache: pthread_spin_lock %s", strerror(err)));
        abort();
    }

    storage = c->storage;
    storage->hash_func = murmur_hash_3;
    storage->kv = wcalloc(c->count_basket * sizeof(cache_basket));

    for (int i = 0; i < c->count_basket; ++i) {
        (storage->kv[i]).lock = wcalloc(sizeof(pthread_rwlock_t));
        err = pthread_rwlock_init((storage->kv[i]).lock, NULL);
        if (err != 0) {
            ereport(INFO, errmsg("init_cache: pthread_mutex_init %s", strerror(err)));
            abort();
        }
    }
}

//A function to retrieve the corresponding cache bucket based on a string.
cache_basket* get_basket(char* key, int key_size) {
    kv_storage* storage;
    u_int64_t hash;

    storage = c->storage;
    hash = storage->hash_func(key, key_size, NULL);
    return &(storage->kv[hash]);
}

/*
* The function checks whether the specified bucket contains data with the provided key.
* If the data exists, a reference to it is returned; otherwise, NULL is returned.
*/
cache_data* find_data_in_basket(cache_basket* basket, char* key, int key_size) {
    cache_data* data;

    data = basket->first;

    while (data != NULL) {
        if (memcmp(data->key, key, key_size) == 0 && data->key_size == key_size) {
            return data;
        }
        data = data->next;
    }
    return NULL;
}

/* A function to check the TTL.
* If the data storage time has expired, the data is removed from the cache;
* if not, the time is updated.
*/
bool check_ttl(cache_basket* basket, cache_data* prev_data, cache_data* data) {
    time_t cur_time;

    if (config.c_conf.ttl_s == 0 ) {
        return true;
    }

    cur_time = time(NULL);
    if (cur_time == -1) {
        char* err = strerror(errno);
        ereport(INFO, errmsg("get_cache: time error  %s", err));
        abort();
    }

    if (cur_time - data->last_time > config.c_conf.ttl_s ) {
        if (prev_data == NULL) {
            basket->first = data->next;
        } else {
            prev_data->next = data->next;
        }

        if (basket->last == data) {
            basket->last = prev_data;
        }

        free_data_from_cache(data);
        return false;
    }

    data->last_time = cur_time;

    return true;
}

void cache_timer_delete(time_t check_time) {
    time_t cur_time = time(NULL);
    for (int i = 0; i < c->count_basket; ++i) {
        cache_basket* basket = &(c->storage->kv[i]);
        cache_data* cur_data;
        cache_data* prev_data;

        basket_lock(basket, false);

        cur_data = basket->first;
        prev_data = NULL;
        while (cur_data != NULL) {
            cache_data* next_data = cur_data->next;
            if (cur_time - cur_data->last_time >= check_time)  {
                prev_data->next = next_data;
                free_data_from_cache(cur_data);
            } else {
                prev_data = cur_data;
            }
            cur_data = next_data;
        }

        basket_unlock(basket);
    }
}

/*
* A function to retrieve data from the cache.
* The function copies the data and returns a pointer to the copied data if the data is found.
* If the data does not exist or its TTL has expired, it returns NULL.
*/
value* get_cache(char* key, int key_size) {
    cache_basket* basket;
    cache_data* data;
    cache_data* prev_data;
    value* result;

    result = NULL;

    basket = get_basket(key, key_size);

    basket_lock(basket, true);
    data = basket->first;
    prev_data = NULL;
    while (data != NULL) {
        if (memcmp(data->key, key, key_size) == 0 && data->key_size == key_size) {
            break;
        }
        prev_data = data;
        data = data->next;
    }


    if (data != NULL && check_ttl(basket, prev_data, data)) {
        result = create_copy_data(data->v);
    }

    basket_unlock(basket);
    return result;
}

/* A function to set new data by key.
* It accepts a structure describing the data.
* First, it checks whether such data already exists.
* If it does, the data is updated; if not, new data is added.
*/
void set_cache(cache_data* new_data) {
    cache_basket* basket;
    cache_data* data;

    basket = get_basket(new_data->key, new_data->key_size);

    basket_lock(basket, false);

    data = find_data_in_basket(basket, new_data->key, new_data->key_size);
    if (data == NULL) {
        if (basket->first == NULL) {
            data = basket->first = basket->last = wcalloc(sizeof(cache_data));
        } else {
            basket->last->next = wcalloc(sizeof(cache_data));
            data = basket->last = basket->last->next;
        }
        data->next = NULL;
        data->key_size = new_data->key_size;
        data->key = new_data->key;
        data->v = new_data->v;
    } else {
        sub_cache_size(data->cache_data_size);
        free_values(data->v);
        data->v = new_data->v;
    }

    if (add_cache_size(data->cache_data_size) == NEED_GC) {
        wake_up_cache_gc();
    }

    data->last_time = time(NULL);
    if (data->last_time == -1) {
        char* err = strerror(errno);
        ereport(INFO, errmsg("set_cache: time error  %s", err));
        abort();
    }
    basket_unlock(basket);
}

int delete_cache(char* key, int key_size) {
    cache_basket* basket;
    cache_data* data;
    cache_data* prev_data;

    basket = get_basket(key, key_size);

    basket_lock(basket, false);

    data = basket->first;
    prev_data = NULL;
    while (data != NULL) {
        if (memcmp(data->key, key, key_size) == 0 && data->key_size == key_size) {
            break;
        }
        prev_data = data;
        data = data->next;
    }

    if (data == NULL) {
        basket_unlock(basket);
        return 0;
    } else if (data == basket->first) {
        basket->first = data->next;
    } else {
        prev_data->next = data->next;
    }

    if (data->next == NULL) {
        basket->last = prev_data;
    }

    free_data_from_cache(data);

    basket_unlock(basket);
    return 1;
}

void free_cache(void) {
    int err;

    for (int i = 0; i < c->count_basket; ++i) {
        cache_basket* basket = &(c->storage->kv[i]);
        cache_data* cur_data = basket->first;
        while (cur_data != NULL) {
            cache_data* new_data = cur_data->next;
            free_data_from_cache(cur_data);
            cur_data = new_data;
        }
        err = pthread_rwlock_destroy(basket->lock);
        if (err != 0) {
            ereport(INFO, errmsg("free_cache: pthread_mutex_destroy %s", strerror(err)));
            abort();
        }
        free((void*) basket->lock);
    }
    err = pthread_spin_destroy(c->size_lock);
    if (err != 0) {
        ereport(INFO, errmsg("free_cache: pthread_spin_destroy %s", strerror(err)));
        abort();
    }

    free((void*)c->size_lock);
    free(c->storage->kv);
    free(c->storage);
    free(c);
}

