#ifndef CACHE_H
#define CACHE_H

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "cache_serializer.h"
#include "config.h"
#include "ht.h"
#include "storage_data.h"

typedef struct cache cache;

data_version* get_cache(key_info* key_i);
int delete_cache(key_info* key_i);
size_t get_cur_cache_size(void);
void cache_clean(int del_time_s);
void free_cache(void);
void init_cache(void);
void invalidate_cache(key_info* key_i, cache_response* v, int value_size, invalidate_mode mode);
void set_cache(key_info* key_i, cache_response* v, int value_size, int ttl_ms);

struct cache {
    _Atomic int table_max_num;
    hash_table* tables;
    hash_table* values;
};

#endif
