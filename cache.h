#ifndef CACHE_H
#define CACHE_H

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "config.h"
#include "ht.h"
#include "storage_data.h"


typedef struct cache cache;
typedef struct table_value table_value;


int delete_cache(char* key, int key_size);
size_t get_cur_cache_size(void);
value* get_cache(key_info* key_i);
void cache_timer_delete(time_t check_time);
void free_cache(void);
void init_cache(void);
void set_cache(cache_data* new_data);

struct table_value {
    hash_table* values;
};

struct cache {
    hash_table* tables;
};

#endif
