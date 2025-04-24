#ifndef HT_H
#define HT_H

#include <stdint.h>
#include <time.h>
#include <stdatomic.h>

typedef struct hash_table hash_table;
typedef struct ht_basket ht_basket;
typedef struct ht_data ht_data;

hash_table* create_ht(int count_basket, size_t max_ht_size, int ttl_s, uint64_t (*hash_func)(void* key, int len, void* argv));
int delete_data(hash_table* ht, char* key, int key_size);
void set_data(hash_table* ht, ht_data* new_data);
void* get_data(hash_table* ht, char* key, int key_size);

struct ht_data {
    void (*value_free)(void* value);
    void (*copy)(void* value);

    void* value;
    ht_data* next;
    char* key;
    int key_size;
    time_t last_time;
    size_t ht_data_size;
};

struct ht_basket {
    ht_data* first;
    ht_data* last;
    pthread_rwlock_t* lock;
};

struct hash_table {
    size_t max_ht_size;
    _Atomic size_t cur_ht_size;
    uint64_t (*hash_func)(void* key, int len, void* argv);
    ht_basket* baskets;
    int count_baskets;
    int ttl_s;
};

#endif