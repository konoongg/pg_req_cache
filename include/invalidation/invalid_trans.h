#ifndef INVALID_POOL_H
#define INVALID_POOL_H

#include <stdatomic.h>

#include "cache_serializer.h"
#include "ht.h"

typedef enum trans_status trans_status;
typedef struct cache_invalidate cache_invalidate;
typedef struct invalidate invalidate;
typedef struct trans_invalidate trans_invalidate;

trans_status check_trans_status(size_t xid);
void add_trans_event(key_info* key_i, created_cache_respons* res, size_t xid, bool is_fuul_v);
void finish_trans_pool(void);
void init_trans_pool(void);
void process_apply(size_t xid);

#define INVALIDATE_XID_POOL_SIZE 101

struct invalidate {
    pthread_rwlock_t* lock;
    trans_invalidate* first;
    trans_invalidate* last;
};

enum trans_status {
    IN_PROGRES,
    COMMIT,
    ABORT,
};

struct trans_invalidate {
    size_t xid;
    trans_invalidate* next;
    trans_invalidate* prev;
    _Atomic int counter;
    _Atomic trans_status status;
};

struct cache_invalidate {
    invalidate* trans_pool;
};

#endif