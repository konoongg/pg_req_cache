#ifndef CACHE_SE_H
#define CACHE_SE_H

#include "libpq-fe.h"

#include "storage_data.h"

typedef struct key_info key_info;

created_cache_respons* create_response_by_pg(PGresult* result, char* table);
created_cache_respons* create_response_by_resp(char* table, char* value, int value_size);
key_info* create_key_info(char* key, int key_size);
void destroy_key_info(key_info* key_i);

struct key_info {
    char* full_key;
    int full_size;

    char* table_column;
    int table_column_size;

    char* table;
    int table_size;

    char* column;
    int column_size;

    char* value;
    int value_size;
};

#endif