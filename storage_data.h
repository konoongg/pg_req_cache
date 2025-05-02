#ifndef STORAGE_DATA_H
#define STORAGE_DATA_H

#include <stdbool.h>

#include "libpq-fe.h"


typedef struct req_column req_column;
typedef struct req_table req_table;


req_table* create_req_by_pg(PGresult* res, char* table);
req_table* create_req_by_resp(char* value, int value_size);
value* create_copy_data(value* v);
void free_req(req_table* req);
void free_values(value* v);



struct req_column {
    char* column_name;
    int data_size;
    char* data;
};

/*
* An intermediate data structure that is formed based
* on data received from the database or the user.
* Metadata about the columns is then added to it,
* and the data for the cache is generated.
*/
struct req_table {
    char* table;
    int count_fields;
    int count_tuples;
    req_column** columns;
};

#endif