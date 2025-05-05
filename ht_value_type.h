#ifndef HT_VALUE_H
#define HT_VALUE_H

#include "db.h"
#include "ht.h"



typedef struct find_value_key  find_value_key;
typedef struct string string;

bool cmp_value_key(void* find_key_1, void* find_key_2);
void free_data_value(void (*value_free)(void* v), ht_data* data);
void value_free_value(void* data);
void value_free_value(void* data);
void* copy_value(void* data);

struct find_value_key {
    char* key;
    int key_size;
    int table_num;
};

struct string {
    char* str;
    int size;
};

union db_data {
    int num;
    string str;
};


#endif