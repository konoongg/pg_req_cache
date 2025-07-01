#ifndef INVALID_POOL_H
#define INVALID_POOL_H

#include "cache_serializer.h"
#include "ht.h"

typedef struct cache_invalidate cache_invalidate;
typedef struct invalidate invalidate;
typedef struct xid_invalidate xid_invalidate;

bool check_inv_xid(size_t xid);
void add_xid_event(key_info* key_i, size_t xid, ht_data* data);
void delete_xid(xid_invalidate* cur);
void init_inv_pool(void);
void process_apply(size_t xid);
void process_reset(size_t xid);

#define INVALIDATE_XID_POOL_SIZE 100

struct invalidate {
    pthread_rwlock_t* lock;
    xid_invalidate* first;
    xid_invalidate* last;
};

struct xid_invalidate {
    size_t xid;
    xid_invalidate* next;
    xid_invalidate* prev;
    ht_data* first_data;
    ht_data* last_data;
};

struct cache_invalidate {
    invalidate* xid_inv;
};

#endif