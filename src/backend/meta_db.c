#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "alloc.h"
#include "config.h"
#include "logger.h"
#include "meta_db.h"
#include "shmem.h"

extern config_cache config;
extern shared_struct* shmem_data;
db_meta_data* meta;


//Creating a query to retrieve information about the table.
static char* create_t_info_req(char* table_name) {
    int req_size;
    char* req;
    char* table_info;

    req_size = strlen(table_name) + TABLE_INFO_SIZE;
    req = wcalloc(req_size * sizeof(char));

    table_info =
        "SELECT "
        "    c.column_name, "
        "    c.data_type, "
        "    c.is_nullable, "
        "    EXISTS ( "
        "        SELECT 1 FROM information_schema.table_constraints tc "
        "        JOIN information_schema.key_column_usage kcu "
        "            ON tc.constraint_name = kcu.constraint_name "
        "            AND tc.table_schema = kcu.table_schema "
        "        WHERE tc.table_name = c.table_name "
        "            AND kcu.column_name = c.column_name "
        "            AND tc.constraint_type = 'PRIMARY KEY' "
        ") AS is_primary_key "
        "FROM information_schema.columns c "
        "WHERE c.table_name = '%s'";

    if (sprintf(req, table_info, table_name) < 0) {
        cache_log(CACHE_ERROR, "create_t_info_req: can't create req");
    }

    return req;
}

// Creating a connection query to PostgreSQL
char* create_conn_req(void) {
    int conn_info_size;
    char* conn_info;

    conn_info_size = CONN_INFO_DEFAULT_SIZE + strlen(config.db_conf.dbname) + strlen(getlogin());
    conn_info = wcalloc(conn_info_size * sizeof(char));
    if (sprintf(conn_info, "user=%s dbname=%s host=localhost", getlogin(), config.db_conf.dbname) < 0) {
        cache_log(CACHE_ERROR, "create_conn_req: can't create connection info");
    }
    return conn_info;
}

// Retrieve all tables and their columns along with their data types.
void init_meta_data(void) {
    PGresult* res;
    PGconn* conn;
    char* conn_info;
    const char* query;

    cache_log(CACHE_INFO , "start init meta data");

    conn_info = create_conn_req();
    conn = PQconnectdb(conn_info);
    free(conn_info);

    shmem_data->meta = meta = shalloc(sizeof(db_meta_data));

    query = "SELECT tablename FROM pg_tables WHERE schemaname = 'public';";

    res = PQexec(conn, query);
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        cache_log(CACHE_ERROR, "init db meta data: SELECT failed: %s", PQerrorMessage(conn));
    }

    meta->count_tables = PQntuples(res);

    cache_log(CACHE_INFO, "init_meta_data: find %d tables",  meta->count_tables);


    meta->tables = shalloc(meta->count_tables * sizeof(table));
    for (int i = 0; i < meta->count_tables; ++i) {
        char* table_name = PQgetvalue(res, i, 0);
        int name_size = PQgetlength(res, i, 0);
        meta->tables[i].name = shalloc( (name_size + 1) * sizeof(char));
        memcpy(meta->tables[i].name, table_name, name_size);
        meta->tables[i].name[name_size] = '\0';
        cache_log(CACHE_INFO, "init_meta_data: find table %s",  meta->tables[i].name);
    }
    PQclear(res);
    for (int i = 0; i < meta->count_tables; ++i) {
        table* t = &(meta->tables[i]);
        int req_oid_size;
        char* req_oid;
        char* table_oid;
        char* bad_int_pars;
        char* text_oid;
        char* query_t_info;

        req_oid_size = strlen(t->name) + TABLE_OID_SIZE;
        req_oid = wcalloc(req_oid_size * sizeof(char));
        table_oid = "SELECT oid FROM pg_class WHERE relname = '%s' AND relkind = 'r'";
        if (sprintf(req_oid, table_oid, t->name) < 0) {
            cache_log(CACHE_ERROR, "create_t_info_req: can't create req");
        }

        res = PQexec(conn, req_oid);
        if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            PQclear(res);
            PQfinish(conn);
            cache_log(CACHE_ERROR, "init db meta data: SELECT failed: %s", PQerrorMessage(conn));
        }
        free(req_oid);
        if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            PQclear(res);
            PQfinish(conn);
            cache_log(CACHE_ERROR, "init db meta data: SELECT failed: %s", PQerrorMessage(conn));
        }

        text_oid = PQgetvalue(res, 0, 0);
        t->oid  = strtoul(text_oid, &bad_int_pars, 10);
        if (*bad_int_pars != '\0' || errno == EINVAL || errno == ERANGE) {
            cache_log(CACHE_ERROR,"create_t_info_req: can't create req");
        }

        PQclear(res);

        query_t_info = create_t_info_req(t->name);

        res = PQexec(conn, query_t_info);
        free(query_t_info);
        if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            cache_log(CACHE_ERROR, "init db meta data: SELECT failed: %s", PQerrorMessage(conn));
        }

        t->count_column = PQntuples(res);

        if (t->count_column == 0) {
            cache_log(CACHE_ERROR, "init_meta_data table %s don't have column", t->name);
        }

        t->columns = shalloc(t->count_column  * sizeof(column));
        for (int c = 0; c < t->count_column; ++c) {
            char* column_name = PQgetvalue(res, c, 0);
            char* is_key = PQgetvalue(res, c, 3);
            char* type = PQgetvalue(res, c, 1);
            int column_name_size = PQgetlength(res, c, 0);

            t->columns[c].column_name = shalloc((column_name_size + 1) * sizeof(char));
            memcpy(t->columns[c].column_name, column_name, column_name_size );
            t->columns[c].column_name[column_name_size] = '\0';

            if (strncmp(type, "text", 4) == 0) {
                t->columns[c].type = STRING;
            } else if (strncmp(type, "integer", 7) == 0) {
                t->columns[c].type = INT;
            } else {
                PQclear(res);
                PQfinish(conn);
                cache_log(CACHE_ERROR, "init_meta_data: undefined type: %s  column_name: %s table %s", type, column_name, t->name);
            }

            if (strncmp(is_key, "t", 1) == 0) {
                t->columns[c].is_key = true;
            } else if (strncmp(is_key, "f", 1) == 0) {
                t->columns[c].is_key = false;
            } else {
                PQclear(res);
                PQfinish(conn);
                cache_log(CACHE_ERROR, "init_meta_data: undefined key: %s", is_key);
            }
        }
        PQclear(res);
    }
    PQfinish(conn);
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

table* get_table_info_by_name(char* table_name) {
    for (int i = 0; i < meta->count_tables; ++i) {
        table* t  = &(meta->tables[i]);
        if (strcmp(table_name, t->name) == 0) {
            return t;
        }
    }
    return NULL;
}


table* get_table_info_by_oid(size_t oid) {
     for (int i = 0; i < meta->count_tables; ++i) {
        table* t  = &(meta->tables[i]);
        if (t->oid == oid) {
            return t;
        }
    }
    return NULL;
}


int get_column_index(char* column_name, char* table_name) {
    table* t = get_table_info_by_name(table_name);
    for (int j = 0; j < t->count_column; ++j) {
        column* c = &(t->columns[j]);
        if (strcmp(column_name, c->column_name) == 0) {
            return j;
        }
    }
    return -1;
}


column* get_key_column(size_t table_oid) {
    table* t = get_table_info_by_oid(table_oid);
     for (int j = 0; j < t->count_column; ++j) {
        column* c = &(t->columns[j]);
        if (c->is_key) {
            return c;
        }
    }
    return NULL;
}

bool table_filter(size_t oid) {
    return get_table_info_by_oid(oid) != NULL;
}
