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

#include "alloc.h"
#include "cache_serializer.h"
#include "cache.h"
#include "config.h"
#include "db.h"
#include "hash.h"
#include "ht_response_type.h"
#include "ht_table_type.h"
#include "ht.h"
#include "logger.h"
#include "shmem.h"
#include "storage_data.h"

cache* c;
extern config_cache config;
extern shared_struct* shmem_data;

void init_cache(void) {
    create_ht_info ht_table_info;
    create_ht_info ht_value_info;

    c = shmem_data->c = shalloc(sizeof(cache));


    atomic_store(&c->table_max_num, 0);

    ht_table_info.cmp_key = cmp_table_key;
    ht_table_info.copy = copy_table;
    ht_table_info.count_basket =  config.c_conf.count_basket_tables;
    ht_table_info.free_data = free_data_table;
    ht_table_info.hash_func = murmur_hash_3;
    ht_table_info.max_ht_size = config.c_conf.max_storage_size;
    ht_table_info.ttl_s = 0;
    ht_table_info.value_free = value_free_table;
    c->tables = create_ht(&ht_table_info);

    ht_value_info.cmp_key = cmp_response_key;
    ht_value_info.copy = copy_response;
    ht_value_info.count_basket = config.c_conf.count_basket_values;
    ht_value_info.free_data = free_data_response;
    ht_value_info.hash_func = murmur_hash_3;
    ht_value_info.max_ht_size = config.c_conf.max_storage_size;
    ht_value_info.ttl_s = config.c_conf.ttl_s;
    ht_value_info.value_free = value_free_response;
    c->values = create_ht(&ht_value_info);

}

static data_version* get_table_column(key_info* key_i) {
    find_table_key ft_key;
    find_ht_data find_table;

    ft_key.key = key_i->table_column;
    ft_key.key_size = key_i->table_column_size;

    find_table.find_key = &ft_key;
    find_table.hash_key = key_i->table_column;
    find_table.hash_key_size = key_i->table_column_size;
    return get_data(c->tables, &find_table);
}

static create_ht_data prepare_value(key_info*  key_i, cache_response* v, int value_size, int table_num, int ttl_ms) {
    create_ht_data new_value_data;
    find_value_key* f_value;
    f_value = shalloc(sizeof(find_value_key));
    f_value->table_num = table_num;
    f_value->key_size = key_i->value_size;
    f_value->key = shalloc(f_value->key_size * sizeof(char));
    memcpy(f_value->key, key_i->value, f_value->key_size);
    new_value_data.find_key = f_value;
    new_value_data.find_key_size = sizeof(find_value_key) + f_value->key_size;
    new_value_data.expire_ms = ttl_ms;

    new_value_data.hash_key_size = key_i->full_size;
    new_value_data.hash_key = key_i->full_key;
    new_value_data.value = v;
    new_value_data.value_size = value_size;
    return new_value_data;
}

/*
* A function to retrieve data from the cache.
* The function copies the data and returns a pointer to the copied data if the data is found.
* If the data does not exist, it returns NULL.
*/
data_version* get_cache(key_info* key_i) {
    data_version* result;

    find_ht_data find_value;
    data_version* table_values_cur_v;
    table_data* table_values;
    find_value_key fv_key;

    table_values_cur_v = get_table_column(key_i);
    if (table_values_cur_v == NULL) {
        return NULL;
    }
    table_values = table_values_cur_v->value;

    fv_key.key = key_i->value;
    fv_key.key_size = key_i->value_size;
    fv_key.table_num = table_values->uniq_num;

    find_value.find_key = &fv_key;
    find_value.hash_key = key_i->full_key;
    find_value.hash_key_size = key_i->full_size;

    result = get_data(c->values, &find_value);
    drop_version(table_values_cur_v);
    return result;
}

static data_version* get_or_create_table(key_info* key_i) {
    data_version* t_values_cur_v = get_table_column(key_i);
    while (t_values_cur_v == NULL) {
        create_ht_data new_table_data;
        table_data* td;
        find_table_key* f_table = shalloc(sizeof(find_table_key));

        f_table->key_size = key_i->table_column_size;
        f_table->key = shalloc(f_table->key_size * sizeof(char));
        memcpy(f_table->key, key_i->table_column, f_table->key_size);

        new_table_data.find_key = f_table;
        new_table_data.find_key_size = sizeof(find_table_key) + f_table->key_size;
        new_table_data.hash_key_size = key_i->table_column_size;
        new_table_data.hash_key = key_i->table_column;
        new_table_data.expire_ms = 0;

        td = shalloc(sizeof(table_data));
        td->uniq_num = atomic_fetch_add(&(c->table_max_num), 1);
        new_table_data.value = td;
        new_table_data.value_size = sizeof(table_data);

        set_data_if_not_exist(c->tables, &new_table_data);
        t_values_cur_v = get_table_column(key_i);
    }
    return t_values_cur_v;
}

/* A function to set new data by key.
* It accepts a structure describing the data.
* First, it checks whether such data already exists.
* If it does, the data is updated; if not, new data is added.
*/
void set_cache(key_info* key_i, cache_response* v, int value_size, int ttl_ms) {
    data_version* t_values_cur_v = get_or_create_table(key_i);
    table_data* t_values = t_values_cur_v->value;
    create_ht_data new_value_data = prepare_value(key_i, v, value_size, t_values->uniq_num, ttl_ms);
    set_data(c->values, &new_value_data);
    drop_version(t_values_cur_v);
}

static db_data* copy_data(db_data* data, db_type type, int* value_size) {
    db_data* new_data = shalloc(sizeof(db_data));
    *value_size += sizeof(db_data);
    switch (type) {
        case INT:
            new_data->num = data->num;
            break;
        case STRING:
            new_data->str.size = data->str.size;
            new_data->str.str = shalloc(new_data->str.size * sizeof(char));
            *value_size += new_data->str.size * sizeof(char);
            memcpy(new_data->str.str, data->str.str, new_data->str.size);
            break;
    }

    return new_data;
}

/*
 * General invalidation logic:
 *
 * Two types of requests may arrive: data update and data deletion
 *
 * For update requests: we take the previous version and use it to populate
 * missing fields in the new version, then add a new invalidated version.
 * Since version retrieval increments its usage counter, we guarantee the garbage
 * collector won't remove it. Set/Del commands modifying data through cache or
 * other backends cannot interfere because concurrent transactions on the same
 * key will be serialized - one will wait for the other to complete.
 *
 * For delete requests: we set a special flag marking the record for deletion.
 *
 * In all cases, we set the new transaction's xid. This is needed to determine
 * data validity during subsequent accesses.
 *
 * Possible operations:
 *
 * 1) Data retrieval:
 *    - First check status:
 *      * If valid: use latest version
 *      * If invalid: use invalidated version
 *      * If unknown: search for the transaction
 *        - If not found: transaction completed successfully
 *        - If found: check status
 *          * If in progress: return current version
 *          * If aborted: decrement usage counter
 *            - When counter reaches 0, garbage collector will reclaim it
 *          * If completed: set xid and status to indicate whether to use
 *            invalidated data or latest version
 *    - Special handling required for data marked for deletion during invalidation
 *
 * 2) Data insertion:
 *    - Two possible paths:
 *      * Direct via Set command
 *      * Through invalidation when adding new values
 *    - Must check if previous invalidation flags are set (deletion is lazy)
 *    - By this point, previous invalidation must have completed - either
 *      applied or aborted. Check as in point #1.
 *      * If applied: move invalidated version to regular version list
 *      * If aborted: simply clear it
 *    - Key differences between cases:
 *      * Set command: can decrement counter if previous transaction aborted
 *        (uses rwlock - write lock only taken for new transaction addition
 *        and garbage collection - deadlocks avoided as only one exclusive
 *        lock exists at any time)
 *      * Invalidation: cannot do this because hash table transaction buckets
 *        are locked, potentially causing bucket collision
 *        - Corresponding functions return previous transaction's xid if exists,
 *          or -1 if record not found
 *
 * 3) Data deletion:
 *    - Three possible methods:
 *      * Garbage collector:
 *        - Doesn't clean records marked with unknown invalidation status
 *        - Only cleans versions within them, not the data structure itself
 *      * Direct user request:
 *        - No need to check previous transaction status
 *        - If we can access the key, no transaction holds it, meaning all
 *          previous transactions completed (successfully or not)
 *        - Data can be deleted regardless
 *      * Invalidation during get request:
 *        - Must check if data was marked for deletion by invalidation
 *        - If transaction succeeded: delete data (like expired TTL handling)
 *        - If transaction failed: clear the deletion flag
 */
size_t invalidate_cache(key_info* key_i, cache_response* v, int value_size, size_t xid) {
    data_version* current_version = get_cache(key_i);
    data_version* t_values_cur_v;
    table_data* t_values;
    create_ht_data new_value_data;
    size_t prev_inv_xid;
    cache_response* prev_v;


    if (!current_version) {
        return -1;
    }

    t_values_cur_v = get_or_create_table(key_i);
    t_values = t_values_cur_v->value;
    prev_v = current_version->value;

    for (int i = 0; i < v->count_fields; ++i) {
        if (v->columns[i] == NULL) {
            v->columns[i] = prev_v->columns[i];
            v->values[i]->data = copy_data(prev_v->values[i]->data, prev_v->columns[i]->type, &value_size);
        }
    }

    new_value_data = prepare_value(key_i, v, value_size, t_values->uniq_num, 0);
    prev_inv_xid = set_invalid_data(c->values, &new_value_data, xid);
    if (prev_inv_xid == INV_DATA_NOT_FOUND) {
        value_free_response(v);
    }

    drop_version(t_values_cur_v);
    drop_version(current_version);

    return prev_inv_xid;
}

int delete_cache(key_info* key_i) {
    int res;

    find_ht_data find_table;
    find_ht_data find_value;

    find_table_key ft_key;
    find_value_key fv_key;

    data_version* values_cur_v;
    table_data* values;

    ft_key.key = key_i->table_column;
    ft_key.key_size = key_i->table_column_size;

    find_table.find_key = &ft_key;
    find_table.hash_key = key_i->table_column;
    find_table.hash_key_size = key_i->table_column_size;

    values_cur_v = get_data(c->tables, &find_table);
    if (values_cur_v == NULL) {
        return 0;
    }
    values = values_cur_v->value;
    fv_key.key = key_i->value;
    fv_key.key_size = key_i->value_size;
    fv_key.table_num = values->uniq_num;

    find_value.find_key = &fv_key;
    find_value.hash_key = key_i->full_key;
    find_value.hash_key_size = key_i->full_size;
    res = delete_data(c->values, &find_value);
    drop_version(values_cur_v);

    return res;
}

void free_cache(void) {
    destroy_ht(c->tables);
    destroy_ht(c->values);
    free(c);
}

size_t get_cur_cache_size(void) {
    return get_cur_size(c->values);
}

void cache_clean(int del_time_s) {
    ht_clean(c->values, del_time_s);
}
