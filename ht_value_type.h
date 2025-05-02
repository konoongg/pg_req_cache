#ifndef HT_VALUE_H
#define HT_VALUE_H

typedef enum db_type db_type;
typedef struct attr attr;
typedef struct find_value_key  find_value_key;
typedef struct string string;
typedef struct value value;
typedef union db_data db_data;

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


enum db_type {
    INT,
    STRING,
};

struct string {
    char* str;
    int size;
};

union db_data {
    int num;
    string str;
};

struct attr {
    db_data* data;
    db_type type;
    char* column_name;
    bool is_nullable;
};

struct value {
    attr** values;
    int count_fields;
    int count_tuples;
};

#endif