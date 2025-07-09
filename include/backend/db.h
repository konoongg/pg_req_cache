#ifndef DB_H
#define DB_H


#include "libpq-fe.h"

#include "cache.h"
#include "storage_data.h"
#include "connection.h"

typedef enum db_oper_res db_oper_res;
typedef struct backend backend;


db_oper_res read_from_db(PGconn* conn, char* t, created_cache_respons** req);
db_oper_res write_to_db (PGconn* conn, char* req);

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

#endif
