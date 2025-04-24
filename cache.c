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
#include "cache.h"
#include "hash.h"
#include "config.h"
#include "storage_data.h"



cache* c;
extern config_redis config;

void init_cache(void) {
    int err;
    int table_cb =  config.c_conf.count_basket_tables;
    size_t max_table_size = config.c_conf.max_storage_size;
    int ttl_s = config.c_conf.ttl_s;

    c = wcalloc(sizeof(cache));

    c->tables = create_ht(table_cb, max_table_size, ttl_s, murmur_hash_2);
}

/*
* A function to retrieve data from the cache.
* The function copies the data and returns a pointer to the copied data if the data is found.
* If the data does not exist, it returns NULL.
*/
value* get_cache(key_info* key_i) {
    cache_basket* basket;
    cache_data* data;
    value* result;

    result = NULL;

    basket = get_basket(key, key_size);

    basket_lock(basket, true);
    data = basket->first;
    while (data != NULL) {
        if (memcmp(data->key, key, key_size) == 0 && data->key_size == key_size) {
            break;
        }
        data = data->next;
    }


    if (data != NULL) {
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
void set_cache(key_info* key_i) {
    hash_table* values = get_data(c->tables, key_i->table_column, key_i->table_column_size);

    if () {
        
    }


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
        atomic_fetch_sub(&(ht->cur_ht_size, value_to_subtract);
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
            ereport(INFO, errmsg("free_cache: pthread_rwlock_destroy %s", strerror(err)));
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

