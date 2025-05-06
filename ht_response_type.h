#ifndef HT_VALUE_H
#define HT_VALUE_H

#include <stdbool.h>

#include "ht.h"

typedef struct find_value_key  find_value_key;

bool cmp_response_key(void* find_key_1, void* find_key_2);
void free_data_response(void (*value_free)(void* v), ht_data* data);
void value_free_response(void* data);
void value_free_response(void* data);
void* copy_response(void* data);

struct find_value_key {
    char* key;
    int key_size;
    int table_num;
};

#endif