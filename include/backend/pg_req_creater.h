#ifndef PG_REQ_H
#define PG_REQ_H

#include "cache_serializer.h"
#include "connection.h"
#include "db.h"

#define SELECT_BASE_SIZE 28
#define SET_BASE_SIZE 56
#define DELETE_BASE_SIZE 23
#define TRANSACTION_SIZE 12

typedef enum attr_parser attr_parser;

char* create_pg_del(int count, key_info** key_i);
char* create_pg_get(key_info* key_i);
char* create_pg_set(key_info* key_i, cache_response* data);

enum attr_parser {
    TABLE,
    COLUMN,
    VALUE,
};

#endif