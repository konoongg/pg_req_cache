#include <stdio.h>
#include <unistd.h>

#include "postgres.h"

#include "libpq-fe.h"

#include "alloc.h"
#include "cache_serializer.h"
#include "cache.h"
#include "config.h"
#include "connection.h"
#include "db.h"
#include "logger.h"
#include "meta_db.h"
#include "storage_data.h"

extern config_cache config;

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
                cache_log(CACHE_ERROR,"read_from_db: bd response error  -  %s", PQresultErrorMessage(result));
                abort();
            }
            result = PQgetResult(conn);
            PQclear(result);
        }

        return READ_OPER_RES;
    }
    return ERR_OPER_RES;
}


static void connect_to_db(backend* backends) {
    char* conn_info = create_conn_req();

    for (int i = 0; i < config.db_conf.count_backend; ++i) {
        backends[i].conn_with_db = PQconnectStart(conn_info);
        if (backends[i].conn_with_db == NULL) {
            cache_log(CACHE_ERROR, "connect_to_db: PQstatus is bad - %s",  PQerrorMessage(backends[i].conn_with_db));
            finish_connects(backends);
            abort();
        }

        if (PQstatus(backends[i].conn_with_db) == CONNECTION_BAD) {
            cache_log(CACHE_ERROR, "connect_to_db: PQstatus is bad - %s",  PQerrorMessage(backends[i].conn_with_db));
            finish_connects(backends);
            abort();
        }
        backends[i].fd = PQsocket(backends[i].conn_with_db);
    }

   free(conn_info);

}


void init_db(backend* back) {
    init_meta_data();
    connect_to_db(back);
}
