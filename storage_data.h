#ifndef STORAGE_H
#define STORAGE_H

typedef enum db_type db_type;
typedef struct cache_attr cache_attr;
typedef struct cache_response cache_response;
typedef struct column column;
typedef struct created_cache_respons created_cache_respons;
typedef union db_data db_data;

struct cache_attr {
    db_data* data;
};

struct cache_response {
    cache_attr** values;
    column** columns;
    int count_fields;
    int count_tuples;
};

struct created_cache_respons {
    cache_response* res;
    int size;
};

struct column {
    db_type type;
    bool is_nullable;
    char* column_name;
};

enum db_type {
    INT,
    STRING,
};

#endif