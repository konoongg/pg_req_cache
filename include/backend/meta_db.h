#ifndef META_DB_H
#define META_DB_H

#include "cache.h"

typedef struct db_meta_data db_meta_data;
typedef struct table table;

#define CONN_INFO_DEFAULT_SIZE 28
#define TABLE_INFO_SIZE 509
#define TABLE_OID_SIZE 64


bool table_filter(size_t oid);
char* create_conn_req(void);
column* get_column_info(char* table_name, char* column_name);
column* get_uniq_column(size_t table_oid);
table* get_table_info(size_t oid);
void init_meta_data(void);

struct db_meta_data {
    table* tables;
    int count_tables;
};

struct table {
    int count_column;
    column* columns;
    char* name;
    size_t oid;
};

#endif