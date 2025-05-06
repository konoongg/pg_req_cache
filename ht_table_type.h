#ifndef HT_TABLE_H
#define HT_TABLE_H

#include <stdbool.h>

#include "ht.h"

typedef struct find_table_key find_table_key;
typedef struct table_data table_data;

bool cmp_table_key(void* find_key_1, void* find_key_2);
void free_data_table(void (*value_free)(void* value), ht_data* data);
void value_free_table(void* value);
void* copy_table(void* value);

struct find_table_key {
    char* key;
    int key_size;
};

struct table_data {
    int uniq_num;
};

#endif