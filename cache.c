#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
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
#include "db.h"
#include "hash.h"
#include "ht_table_type.h"
#include "ht_value_type.h"
#include "ht.h"
#include "storage_data.h.h"
#include "storage_data.h"

cache* c;
extern config_redis config;

void init_cache(void) {
    c = wcalloc(sizeof(cache));

    atomic_store(&c->table_max_num, 0);

    create_ht_info ht_table_info;
    ht_table_info.cmp_key = cmp_table_key;
    ht_table_info.copy = copy_table;
    ht_table_info.count_basket =  config.c_conf.count_basket_tables;
    ht_table_info.free_data = free_data_table;
    ht_table_info.hash_func = murmur_hash_3;
    ht_table_info.max_ht_size = config.c_conf.max_storage_size;
    ht_table_info.ttl_s = 0;
    ht_table_info.value_free = value_free_table;
    c->tables = create_ht(&ht_table_info);

    create_ht_info ht_value_info;
    ht_value_info.cmp_key = cmp_table_key;
    ht_value_info.copy = copy_value;
    ht_value_info.count_basket = config.c_conf.count_basket_values;
    ht_value_info.free_data = free_data_value;
    ht_value_info.hash_func = murmur_hash_3;
    ht_value_info.max_ht_size = config.c_conf.max_storage_size;
    ht_value_info.ttl_s = config.c_conf.ttl_s;
    ht_value_info.value_free = value_free_value;
    c->values = create_ht(&ht_value_info);
}

/*
* A function to retrieve data from the cache.
* The function copies the data and returns a pointer to the copied data if the data is found.
* If the data does not exist, it returns NULL.
*/
cache_response* get_cache(key_info* key_i) {
    hash_table* values = get_data(c->tables, key_i->table_column, key_i->table_column_size);
    if (values == NULL) {
        return NULL;
    }
    cache_response* result = get_data(c->tables, key_i->value, key_i->value_size);
    return result;
}

/* A function to set new data by key.
* It accepts a structure describing the data.
* First, it checks whether such data already exists.
* If it does, the data is updated; if not, new data is added.
*/
void set_cache(key_info* key_i, cache_response* v, int value_size) {
    find_table_key f_table;
    f_table.key = key_i->table_column;
    f_table.key_size = key_i->table_column_size;

    find_ht_data f_data;
    f_data.hash_key = key_i->table_column;
    f_data.hash_key_size = key_i->table_column_size;
    f_data.find_key = &f_table;

    table_data* t_values = get_data(c->tables, &find_ht_data);
    create_ht_data new_data;

    if (t_values == NULL) {
        create_ht_data new_table_data;

        find_table_key* f_table = wcalloc(sizeof(find_table_key));
        f_table->key_size = key_i->table_column_size;
        f_table->key = wcalloc(f_table->key_size * sizeof(char));
        memcpy(f_table->key, key_i->table_column, f_table->key_size);

        new_table_data.find_key = f_table;
        new_table_data.find_key_size = sizeof(find_table_key) + f_table->key_size;
        new_table_data.hash_key_size = key_i->table_column_size;
        new_table_data.hash_key = key_i->table_column;

        table_data* td = wcalloc(sizeof(table_data));
        td->uniq_num = atomic_fetch_add(&(c->table_max_num), 1);
        new_table_data.value = td;
        new_table_data.value_size = sizeof(table_data);

        set_data_if_not_exist(c->tables, &new_table_data);
        t_values = get_data(c->tables, &find_ht_data);
    }
    create_ht_data new_value_data;

    find_value_key* f_value = wcalloc(sizeof(find_value_key));
    f_value->table_num = t_values->uniq_num;
    f_value->key_size = key_i->value_size;
    f_value->key = wcalloc(f_value->key_size * sizeof(char));
    memcpy(f_value->key, key_i->valuel, f_value->key_size);
    new_value_data.find_key = f_value;
    new_value_data.find_key_size = sizeof(find_value_key) + f_value->key_size;

    new_value_data.hash_key_size = key_i->full_size.
    new_value_data.hash_key = key_i->full_key;

    new_value_data.value = v;
    new_value_data.value_size = value_size;
    set_data(c->values, &new_data);
}

int delete_cache(key_info* key_i) {
    hash_table* values = get_data(c->tables, key_i->table_column, key_i->table_column_size);
    if (values == NULL) {
        return 0;
    }
    return delete_data(values, key_i->value, key_i->value_size);
}

void free_cache(void) {
    destroy_ht(c->tables);
    destroy_ht(c->values);
    free(c);
}

