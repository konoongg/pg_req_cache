#ifndef HT_H
#define HT_H

#include <stdint.h>
#include <time.h>
#include <stdatomic.h>

typedef enum invalidate_mode invalidate_mode;
typedef struct create_ht_data create_ht_data;
typedef struct create_ht_info create_ht_info;
typedef struct data_version data_version;
typedef struct find_ht_data find_ht_data;
typedef struct hash_table hash_table;
typedef struct ht_basket ht_basket;
typedef struct ht_data ht_data;
typedef struct invalid_ht_data invalid_ht_data;

data_version* get_data(hash_table* ht, find_ht_data* find);
hash_table* create_ht(create_ht_info* info);
int delete_data(hash_table* ht, find_ht_data* find);
size_t get_cur_size(hash_table* ht);
void destroy_ht(hash_table* ht);
void drop_version(data_version* version);
void ht_clean(hash_table* ht, int recomendate_ttl_s);
void invalidate_data(hash_table* ht, invalid_ht_data* inv);
void set_data_if_not_exist(hash_table* ht, create_ht_data* new_data);
void set_data(hash_table* ht, create_ht_data* new_data);

struct data_version {
    _Atomic int usage_counter;
    bool dirty;
    data_version* next;
    void* value;
};

struct ht_data {
    data_version* value_first;
    data_version* value_cur;

    ht_data* next;
    void* find_key;
    time_t last_time;
    size_t expire_ms;
    size_t ht_data_size;
    int invalid_save;
};

struct create_ht_info {
    bool (*cmp_key)(void* find_key_1, void* find_key_2);
    uint64_t (*hash_func)(void* key, int len, void* argv);
    void (*free_data)(ht_data* data);
    void (*value_free)(void* value);
    void* (*copy)(void* value);

    int count_basket;
    size_t max_ht_size;
    int ttl_s;
};

enum invalidate_mode {
    INV_UPDATE,
    INV_DELETE,
};

struct invalid_ht_data {
    create_ht_data* create;
    find_ht_data* find;
    invalidate_mode mode;
};

struct create_ht_data {
    int value_size;
    int find_key_size;
    int hash_key_size;

    char* hash_key;
    void* find_key;
    void* value;

    size_t expire_ms;
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
    void (*free_data)(ht_data* data);
    void (*value_free)(void* value);
    void* (*copy)(void* value);

    bool not_ttl;
    size_t max_ht_size;
    _Atomic size_t cur_ht_size;
    ht_basket* baskets;
    int count_baskets;
    int ttl_s;
};

#endif