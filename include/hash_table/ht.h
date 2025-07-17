#ifndef HT_H
#define HT_H

#include <stdint.h>
#include <time.h>
#include <stdatomic.h>

typedef enum invalid_status invalid_status;
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
size_t set_invalid_data(hash_table* ht, create_ht_data* new_data, size_t xid);
void drop_version(data_version* version);
void ht_clean(hash_table* ht, int recomendate_ttl_s);
void set_data_if_not_exist(hash_table* ht, create_ht_data* new_data);
void set_data(hash_table* ht, create_ht_data* new_data);

struct data_version {
    _Atomic int usage_counter;
    bool dirty;
    data_version* next;
    void* value;
};

enum invalid_status {
    VALID,
    INVALID,
    UNKNOWN,
};

/*
 * Cache invalidation logic is quite complex. When invalidation occurs:
 * - The cache bucket is locked and marked with a transaction xid (atomic variable)
 * - set operations are simple (just overwrite/add new versions)
 * - get/del operations require careful handling:
 *   * get checks the xid:
 *     - xid=0: no invalidation occurred
 *     - xid≠0: check invalidation pool
 *       - If xid exists: transaction is still in progress (current value can be used)
 *       - Note: Value may change after check, but WAL replay ensures eventual consistency
 *   * This mechanism guarantees cache consistency - transactions are either fully applied or not at all
 *
 * Special cases:
 * - If set/del arrives for uncommitted transaction:
 *   - No problem (means commit/abort occurred but wasn't processed yet)
 *   - Current implementation still invalidates in this case
 * - del operation:
 *   - Removes the version but preserves ht_data if invalidation markers exist
 */
struct ht_data {
    data_version* value_first;
    data_version* value_cur;

    ht_data* next;
    void* find_key;
    time_t last_time;
    size_t expire_ms;
    size_t ht_data_size;

    data_version* inv_value;
    _Atomic size_t xid_inv;
    _Atomic invalid_status inv_status;
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
