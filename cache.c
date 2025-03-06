#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

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
#include "cache.h"
#include "config.h"
#include "hash.h"
#include "storage_data.h"

bool check_ttl(cache_data* data);
cache_basket* get_basket(char* key, int key_size);
cache_data* find_data_in_basket(cache_basket* basket, char* key, int key_size);
void free_storage(kv_storage storage);
void free_data_from_cache(cache_data* data);

cache* c;
extern config_redis config;

// It releases the data from the cache.
void free_data_from_cache(cache_data* data) {
    free_values(data->v);
    free(data->key);
    free(data);
}

void init_cache(void) {
    kv_storage* storage;

    c = wcalloc(sizeof(cache));
    c->storage = wcalloc(sizeof(kv_storage));
    storage = c->storage;

    c->count_basket = config.c_conf.count_basket;
    storage->hash_func = murmur_hash_2;
    storage->kv = wcalloc(c->count_basket * sizeof(cache_basket));

    for (int i = 0; i < c->count_basket; ++i) {
        int err;
        (storage->kv[i]).lock = wcalloc(sizeof(pthread_spinlock_t));
        err = pthread_spin_init(storage->kv[i].lock, PTHREAD_PROCESS_PRIVATE);
        if (err != 0) {
            ereport(INFO, errmsg("init_cache: pthread_spin_lock %s", strerror(err)));
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
bool check_ttl(cache_data* data) {
    time_t cur_time;

    if (config.c_conf.ttl_s == 0 ) {
        return true;
    }

    cur_time = time(NULL);
    if (cur_time == -1) {
        char* err = strerror(errno);
        ereport(INFO, errmsg("get_cache: time error  %s", err));
    }

    if (cur_time - data->last_time > config.c_conf.ttl_s ) {
        free_data_from_cache(data);
        return false;
    }

    data->last_time = cur_time;
    return true;
}

/*
* A function to retrieve data from the cache.
* The function copies the data and returns a pointer to the copied data if the data is found.
* If the data does not exist or its TTL has expired, it returns NULL.
*/
value* get_cache(char* key, int key_size) {
    cache_basket* basket;
    cache_data* data;
    int err;
    value* result;

    result = NULL;

    basket = get_basket(key, key_size);
    err = pthread_spin_lock(basket->lock);
    if (err != 0) {
        ereport(INFO, errmsg("get_cache: pthread_spin_lock %s", strerror(err)));
        abort();
    }

    data = find_data_in_basket(basket, key, key_size);
    if (data != NULL && check_ttl(data)) {
        result = create_copy_data(data->v);
    }

    err = pthread_spin_unlock(basket->lock);
    if (err != 0) {
        ereport(INFO, errmsg("get_cache: pthread_spin_unlock %s", strerror(err)));
        abort();
    }

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
    int err;

    basket = get_basket(new_data->key, new_data->key_size);

    err = pthread_spin_lock(basket->lock);
    if (err != 0) {
        ereport(INFO, errmsg("get_cache: pthread_spin_lock %s", strerror(err)));
        abort();
    }

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
        free_values(data->v);
        data->v = new_data->v;
    }

    data->last_time = time(NULL);
    if (data->last_time == -1) {
        char* err = strerror(errno);
        ereport(INFO, errmsg("set_cache: time error  %s", err));
        abort();
    }

    err = pthread_spin_unlock(basket->lock);
    if (err != 0) {
        ereport(INFO, errmsg("set_cache: pthread_spin_unlock %s", strerror(err)));
        abort();
    }
}

int delete_cache(char* key, int key_size) {
    cache_basket* basket;
    cache_data* data;
    cache_data* prev_data;
    int err;

    basket = get_basket(key, key_size);

    err = pthread_spin_lock(basket->lock);
    if (err != 0) {
        ereport(INFO, errmsg("delete_cache: pthread_spin_lock %s", strerror(err)));
        abort();
    }

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
        err = pthread_spin_unlock(basket->lock);
        if (err != 0) {
            ereport(INFO, errmsg("delete_cache: pthread_spin_unlock %s", strerror(err)));
            abort();
        }
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

    err = pthread_spin_unlock(basket->lock);
    if (err != 0) {
        ereport(INFO, errmsg("delete_cache: pthread_spin_unlock %s", strerror(err)));
        abort();
    }

    return 1;
}

void free_cache(void) {
    for (int i = 0; i < c->count_basket; ++i) {
        cache_basket* basket = &(c->storage->kv[i]);
        cache_data* cur_data = basket->first;
        while (cur_data != NULL) {
            cache_data* new_data = cur_data->next;
            free_data_from_cache(cur_data);
            cur_data = new_data;
        }
    }
    free(c->storage->kv);
    free(c->storage);
    free(c);
}

