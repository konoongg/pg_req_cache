#ifndef CACHE_H
#define CACHE_H

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "config.h"
#include "ht_value_type.h"
#include "ht.h"
#include "storage_data.h"


typedef struct cache cache;
typedef struct key_info key_info;
typedef struct table_value table_value;

int delete_cache(char* key, int key_size);
size_t get_cur_cache_size(void);
value* get_cache(key_info* key_i);
void cache_timer_delete(time_t check_time);
void free_cache(void);
void init_cache(void);
void set_cache(cache_data* new_data);

struct cache {
    hash_table* tables;
    hash_table* values;
};

struct key_info {
    char* full_key;

    char* table_column;
    char* value;
    int table_column_size;
    int value_size;
};

#endif
