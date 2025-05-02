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
#include "hash.h"
#include "ht_table_type.h"
#include "ht_value_type.h"
#include "ht.h"
#include "storage_data.h"

value* create_copy_value(value* v);
void free_values(value* v);

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

//Based on the pre-formed data information, we create data for the cache and add metadata.
value* create_value(char* key, int key_size, req_table* args) {
    cache_data* data = wcalloc(sizeof(cache_data));
    data->cache_data_size = sizeof(cache_data);

    data->key = wcalloc(key_size * sizeof(char));
    data->cache_data_size += key_size * sizeof(char);
    data->key_size = key_size;
    memcpy(data->key, key, key_size);

    data->v = wcalloc(sizeof(value));
    data->cache_data_size += sizeof(value);
    data->v->count_fields = args->count_fields;
    data->v->count_tuples = args->count_tuples;
    data->v->values = wcalloc(args->count_tuples * sizeof(attr*));
    data->cache_data_size += args->count_tuples * sizeof(attr*);
    for (int i = 0; i < args->count_tuples; ++i) {
        data->v->values[i] = wcalloc(args->count_fields * sizeof(attr));
        data->cache_data_size += args->count_fields * sizeof(attr);
        for (int j = 0; j < args->count_fields; ++j ) {
            int column_name_size;
            column* c = get_column_info(args->table, args->columns[i][j].column_name);
            attr* a;

            if (c == NULL) {
                ereport(INFO, errmsg("init_cache_data: can't get column %s in table %s ", args->columns[i][j].column_name, args->table));
                abort();
            }
            column_name_size = strlen(c->column_name) + 1;
            a = &(data->v->values[i][j]);
            a->type = c->type;
            a->column_name = wcalloc(column_name_size * sizeof(char));
            data->cache_data_size += column_name_size * sizeof(char);
            memcpy(a->column_name, args->columns[i][j].column_name, column_name_size);
            a->is_nullable = c->is_nullable;


            a->data = wcalloc(sizeof(db_data));
            data->cache_data_size += sizeof(db_data);
            switch (a->type) {
                case INT:
                    a->data->num = (int)strtol(args->columns[i][j].data, NULL, 10);
                    break;
                case STRING:
                    a->data->str.size = args->columns[i][j].data_size;
                    a->data->str.str = wcalloc(a->data->str.size * sizeof(char));
                    data->cache_data_size += a->data->str.size * sizeof(char);
                    memcpy(a->data->str.str, args->columns[i][j].data, a->data->str.size );
                    break;
            }
        }
    }
    return data;
}


/*
* A function to retrieve data from the cache.
* The function copies the data and returns a pointer to the copied data if the data is found.
* If the data does not exist, it returns NULL.
*/
value* get_cache(key_info* key_i) {
    hash_table* values = get_data(c->tables, key_i->table_column, key_i->table_column_size);
    if (values == NULL) {
        return NULL;
    }
    value* result = get_data(c->tables, key_i->value, key_i->value_size);
    return result;
}

/* A function to set new data by key.
* It accepts a structure describing the data.
* First, it checks whether such data already exists.
* If it does, the data is updated; if not, new data is added.
*/
// надо добавить очситку  hash key
void set_cache(key_info* key_i, value* v, int value_size) {
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
        new_table_data.hash_key = wcalloc(new_table_data.hash_key_size * sizeof(char));
        memcpy(new_table_data.hash_key, key_i->table_column, new_table_data.hash_key_size);

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
    new_value_data.hash_key = wcalloc(new_value_data.hash_key_size  * sizeof(char));
    memcpy(new_value_data.hash_key, key_i->full_key, new_value_data.hash_key_size);

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

