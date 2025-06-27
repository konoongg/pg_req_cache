#ifndef DB_H
#define DB_H

#define CONN_INFO_DEFAULT_SIZE 28
#define TABLE_INFO_SIZE 509
#define TABLE_OID_SIZE 64

#include "libpq-fe.h"

#include "cache.h"
#include "storage_data.h"
#include "connection.h"

typedef enum db_oper_res db_oper_res;
typedef struct backend backend;
typedef struct db_meta_data db_meta_data;
typedef struct table table;

column* get_column_info(char* table_name, char* column_name);
column* get_uniq_column(size_t table_oid);
db_oper_res read_from_db(PGconn* conn, char* t, created_cache_respons** req);
db_oper_res write_to_db (PGconn* conn, char* req);
table* get_table_info(size_t oid);
void finish_connects(backend* backends);
void init_db(backend* back);

enum db_oper_res {
    READ_OPER_RES, // Data successfully read
    WRITE_OPER_RES, // Data successfully written
    WAIT_OPER_RES, // Waiting, the action needs to be repeated
    ERR_OPER_RES, // Error
};

struct backend {
    PGconn* conn_with_db;
    bool is_free;
    int fd;
    connection* conn;
};


struct table {
    int count_column;
    column* columns;
    char* name;
    size_t oid;
};

struct db_meta_data {
    table* tables;
    int count_tables;
};

#endif
