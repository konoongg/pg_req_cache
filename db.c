#include <stdio.h>
#include <unistd.h>

#include "postgres.h"
#include "utils/elog.h"
#include "libpq-fe.h"

#include "alloc.h"
#include "cache_serializer.h"
#include "cache.h"
#include "config.h"
#include "connection.h"
#include "db.h"
#include "storage_data.h"

char* create_conn_req(void);
char* create_t_info_req(char* table_name);
void connect_to_db(backend* backends);
void init_meta_data(void);

extern config_redis config;
db_meta_data* meta;

void finish_connects(backend* backends) {
    for (int i = 0; i < config.db_conf.count_backend; ++i) {
        PQfinish(backends[i].conn_with_db);
    }
}

/*
* Sending a query to the database.
* If the connection is not yet ready to write,
* return WAIT_OPER_RES. If an error occurs, return ERR_OPER_RES.
* If we can write the data, write itand return WRITE_OPER_RES.
*/
db_oper_res write_to_db(PGconn* conn, char* req) {
    if (PQconnectPoll(conn) == PGRES_POLLING_WRITING || PQconnectPoll(conn) == PGRES_POLLING_READING) {
        return WAIT_OPER_RES;
    } else if (PQconnectPoll(conn) == PGRES_POLLING_FAILED) {
        return ERR_OPER_RES;
    } else if (PQconnectPoll(conn) == PGRES_POLLING_OK) {
        if (PQsendQuery(conn, req) != 1) {
            return ERR_OPER_RES;
        }
        return WRITE_OPER_RES;
    }
    return ERR_OPER_RES;
}


/*
* If the data is not yet ready to be read,
* return WAIT_OPER_RES. If an error occurs, return ERR_OPER_RES.
* Otherwise, read the data, create a formatted representation of the data, and return READ_OPER_RES.
* The PQgetResult(conn) function must be called until NULL is returned.
* In the current implementation, it is expected that all data is read at once.
*/
db_oper_res read_from_db(PGconn* conn, char* t, created_cache_respons** res) {
    if (PQconnectPoll(conn) == PGRES_POLLING_WRITING) {
        return WAIT_OPER_RES;
    } else if (PQconnectPoll(conn) == PGRES_POLLING_FAILED) {
        return ERR_OPER_RES;
    } else if (PQconnectPoll(conn) == PGRES_POLLING_OK) {
        PGresult* result = PQgetResult(conn);
        *res = create_response_by_pg(result, t);

        PQclear(result);

        if (*res == NULL) {
            return ERR_OPER_RES;
        }

        while (result != NULL) {
            if (PQresultStatus(result) == PGRES_FATAL_ERROR) {
                const char* errorMessage = PQresultErrorMessage(result);
                ereport(INFO, errmsg("read_from_db: bd response error  -  %s", errorMessage));
                abort();
            }
            result = PQgetResult(conn);
            PQclear(result);
        }

        return READ_OPER_RES;
    }
    return ERR_OPER_RES;
}

// Creating a connection query to PostgreSQL
char* create_conn_req(void) {
    int conn_info_size;
    char* conn_info;

    conn_info_size = CONN_INFO_DEFAULT_SIZE + strlen(config.db_conf.dbname) + strlen(getlogin());
    conn_info = wcalloc(conn_info_size * sizeof(char));
    if (sprintf(conn_info, "user=%s dbname=%s host=localhost", getlogin(), config.db_conf.dbname) < 0) {
        ereport(INFO, errmsg("create_conn_req: can't create connection info"));
        abort();
    }
    return conn_info;
}

//Creating a query to retrieve information about the table.
char* create_t_info_req(char* table_name) {
    int req_size;
    char* req;
    char* table_info;

    req_size = strlen(table_name) + TABLE_INFO_SIZE;
    req = wcalloc(req_size * sizeof(char));
    table_info = "SELECT column_name, data_type, is_nullable FROM information_schema.columns WHERE table_name = '%s'";

    if (sprintf(req, table_info, table_name) < 0) {
        ereport(INFO, errmsg("create_t_info_req: can't create req"));
        abort();
    }

    return req;
}

//The server is busy. Please try again later.
void connect_to_db(backend* backends) {
    char* conn_info = create_conn_req();

    for (int i = 0; i < config.db_conf.count_backend; ++i) {
        backends[i].conn_with_db = PQconnectStart(conn_info);
        if (backends[i].conn_with_db == NULL) {
            ereport(INFO, errmsg("connect_to_db: PQstatus is bad - %s",  PQerrorMessage(backends[i].conn_with_db)));
            finish_connects(backends);
            abort();
        }

        if (PQstatus(backends[i].conn_with_db) == CONNECTION_BAD) {
            ereport(INFO, errmsg("connect_to_db: PQstatus is bad - %s",  PQerrorMessage(backends[i].conn_with_db)));
            finish_connects(backends);
            abort();
        }
        backends[i].fd = PQsocket(backends[i].conn_with_db);
    }

   free(conn_info);

}

/*
* Returns information about the table, the type of the specified column,
* and whether it can be nullable, based on the table name and column name.
*/
column* get_column_info(char* table_name, char* column_name) {
    for (int i = 0; i < meta->count_tables; ++i) {
        table* t  = &(meta->tables[i]);
        if (strncmp(t->name, table_name, strlen(table_name)) == 0 && strlen(table_name) == strlen(t->name)) {
            for (int j = 0; j < t->count_column; ++j) {
                column* c = &(t->columns[j]);
                if (strncmp(c->column_name, column_name, strlen(column_name)) == 0 &&
                    strlen(column_name) == strlen(c->column_name)) {
                    return c;
                }
            }
        }
    }

    return NULL;
}

void init_db(backend* back) {
    init_meta_data();
    connect_to_db(back);
}

// Retrieve all tables and their columns along with their data types.
void init_meta_data(void) {
    PGresult* res;
    PGconn* conn;
    char* conn_info;
    const char* query;
    conn_info = create_conn_req();
    conn = PQconnectdb(conn_info);
    free(conn_info);

    meta = wcalloc(sizeof(db_meta_data));

    query = "SELECT tablename FROM pg_tables WHERE schemaname = 'public';";
    res = PQexec(conn, query);
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        ereport(INFO, errmsg("SELECT failed: %s", PQerrorMessage(conn)));
        PQclear(res);
        PQfinish(conn);
        abort();
    }

    meta->count_tables = PQntuples(res);

    meta->tables = wcalloc(meta->count_tables * sizeof(table));
    for (int i = 0; i < meta->count_tables; ++i) {
        char* table_name = PQgetvalue(res, i, 0);
        int name_size = PQgetlength(res, i, 0);
        meta->tables[i].name = wcalloc( (name_size + 1) * sizeof(char));
        memcpy(meta->tables[i].name, table_name, name_size);
        meta->tables[i].name[name_size] = '\0';
    }
    PQclear(res);
    for (int i = 0; i < meta->count_tables; ++i) {
        table* t = &(meta->tables[i]);
        char* query_t_info = create_t_info_req(t->name);
        res = PQexec(conn, query_t_info);
        free(query_t_info);
        if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            ereport(INFO, errmsg("SELECT failed: %s", PQerrorMessage(conn)));
            PQclear(res);
            PQfinish(conn);
            abort();
        }

        t->count_column = PQntuples(res);
        t->columns = wcalloc(t->count_column  * sizeof(column));
        for (int c = 0; c < t->count_column; ++c) {
            char* column_name = PQgetvalue(res, c, 0);
            char* is_nullable = PQgetvalue(res, c, 2);
            char* type = PQgetvalue(res, c, 1);
            int column_name_size = PQgetlength(res, c, 0);

            t->columns[c].column_name = wcalloc((column_name_size + 1) * sizeof(char));
            memcpy(t->columns[c].column_name, column_name, column_name_size );
            t->columns[c].column_name[column_name_size] = '\0';

            if (strncmp(type, "text", 4) == 0) {
                t->columns[c].type = STRING;
            } else if (strncmp(type, "integer", 7) == 0) {
                t->columns[c].type = INT;
            } else {
                ereport(INFO, errmsg("init_meta_data: undefined type: %s", type));
                PQclear(res);
                PQfinish(conn);
                abort();
            }

            if (strncmp(is_nullable, "NO", 2)) {
                t->columns[c].is_nullable = false;
            } else if (strncmp(is_nullable, "YES", 3)) {
                t->columns[c].is_nullable = true;
            } else {
                ereport(INFO, errmsg("init_meta_data: undefined nullable: %s", is_nullable));
                PQclear(res);
                PQfinish(conn);
                abort();
            }
        }
        PQclear(res);
    }
    PQfinish(conn);
}
