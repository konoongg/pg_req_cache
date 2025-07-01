#ifndef STORAGE_H
#define STORAGE_H

#include "stdbool.h"

typedef enum db_type db_type;
typedef struct cache_attr cache_attr;
typedef struct cache_response cache_response;
typedef struct column column;
typedef struct created_cache_respons created_cache_respons;
typedef struct string string;
typedef union db_data db_data;

struct cache_attr {
    db_data* data;
};

struct cache_response {
    cache_attr** values;
    column** columns; // сохраняем просто сылку, на структуру, которую проинициализоали 
    int count_fields;
    int count_tuples;

    char* prepare_answer;
    int prepare_answer_size;
    bool prepare_answer_valid;
    bool updated; // кажется это поле больше не нужно, так как с версионностью неикто не может обноить конкретную версию

    
};

struct created_cache_respons {
    cache_response* res;
    int size;
};

enum db_type {
    INT,
    STRING,
};

struct column {
    db_type type;
    char* column_name;
    bool is_uniq;
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