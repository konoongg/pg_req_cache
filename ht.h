#ifndef HT_H
#define HT_H

#include <stdint.h>
#include <time.h>
#include <stdatomic.h>

typedef struct create_ht_info create_ht_info;
typedef struct create_ht_data create_ht_data;
typedef struct find_ht_data find_ht_data;
typedef struct hash_table hash_table;
typedef struct ht_basket ht_basket;
typedef struct ht_data ht_data;

hash_table* create_ht(create_ht_info* info);
int delete_data(hash_table* ht, find_ht_data* find);
void destroy_ht(hash_table* ht);
void ht_timer_delete(hash_table* ht, time_t check_time);
void set_data_if_not_exist(hash_table* ht, create_ht_data* new_data);
void set_data(hash_table* ht, create_ht_data* new_data);
void* get_data(hash_table* ht, find_ht_data* find);

struct ht_data {
    void* value;
    ht_data* next;
    void* find_key;
    time_t last_time;
    size_t ht_data_size;
};

struct create_ht_info {
    bool (*cmp_key)(void* find_key_1, void* find_key_2);
    uint64_t (*hash_func)(void* key, int len, void* argv);
    void (*free_data)(void (*value_free)(void* value), ht_data* data);
    void (*value_free)(void* value);
    void* (*copy)(void* value);

    int count_basket;
    size_t max_ht_size;
    int ttl_s;
};

struct create_ht_data {
    int value_size;
    int find_key_size;
    int hash_key_size;

    char* hash_key;
    void* find_key;
    void* value;
};

struct find_ht_data {
    char* hash_key;
    int hash_key_size;
    void* find_key;
};

struct ht_basket {
    ht_data* first;
    ht_data* last;
    pthread_rwlock_t* lock;
};

struct hash_table {
    bool (*cmp_key)(void* find_key_1, void* find_key_2);
    uint64_t (*hash_func)(void* key, int len, void* argv);
    void (*free_data)(void (*value_free)(void* value), ht_data* data);
    void (*value_free)(void* value);
    void* (*copy)(void* value);

    size_t max_ht_size;
    _Atomic size_t cur_ht_size;
    ht_basket* baskets;
    int count_baskets;
    int ttl_s;
};

#endif