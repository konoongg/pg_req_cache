#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/eventfd.h>

#include "postgres.h"
#include "utils/elog.h"
#include "miscadmin.h"

#include "alloc.h"
#include "connection.h"
#include "db.h"
#include "hash.h"
#include "query_cache_controller.h"

db_worker dbw;
extern config_redis config;

command_to_db* get_command(void);
proc_status notify_db(connection* conn);
proc_status process_read_db(connection* conn);
proc_status process_write_db(connection* conn);
void free_db_command(command_to_db* cmd);
void* start_db_worker(void*);

void free_db_command(command_to_db* cmd) {
    free(cmd->key);
    free(cmd->table);
    free(cmd->cmd);
    free(cmd);
}

/*
* Registering a new event for database processing.
* The event is added to the processing queue,
* and the database worker's loop is notified via eventfd that new events have arrived.
*/
void register_command(char* tabl, char* req, connection* conn, com_reason reason, char* key, int key_size) {
    command_to_db* cmd = wcalloc(sizeof(command_to_db));
    int err;

    cmd->next = NULL;
    cmd->conn = conn;
    cmd->table = tabl;
    cmd->reason = reason;
    cmd->cmd = req;

    cmd->key = wcalloc(key_size * sizeof(char));
    memcpy(cmd->key, key, key_size);
    cmd->key_size = key_size;

    err = pthread_spin_lock(dbw.lock);
    if (err != 0) {
        ereport(INFO, errmsg("register_command: pthread_spin_lock %s", strerror(err)));
        abort();
    }

    if (dbw.commands->first == NULL) {
        dbw.commands->first = dbw.commands->last = cmd;
    } else {
        dbw.commands->last->next = cmd;
        dbw.commands->last = dbw.commands->last->next;
    }
    dbw.commands->last->next = NULL;
    dbw.commands->count_commands++;
    event_notify(dbw.wthrd->not);

    err = pthread_spin_unlock(dbw.lock);
    if (err != 0) {
        ereport(INFO, errmsg("register_command: pthread_spin_unlock %s", strerror(err)));
        abort();
    }
}

// Retrieving a command for processing from the queue
command_to_db* get_command(void) {
    int err = pthread_spin_lock(dbw.lock);
    command_to_db* cmd;

    if (err != 0) {
        ereport(INFO, errmsg("register_command: pthread_spin_lock %s", strerror(err)));
        abort();
    }

    cmd = dbw.commands->first;
    dbw.commands->first = dbw.commands->first->next;
    dbw.commands->count_commands--;

    if (dbw.commands->count_commands == 0) {
        dbw.commands->first = dbw.commands->last = NULL;
    }

    err = pthread_spin_unlock(dbw.lock);
    if (err != 0) {
        ereport(INFO, errmsg("register_command: pthread_spin_unlock %s", strerror(err)));
        abort();
    }

    return cmd;
}

// If we have a request, we send it to the database
proc_status process_write_db(connection* conn) {
    backend* back = (backend*)conn->data;
    command_to_db* cmd = conn->w_data->data;
    db_oper_res res = write_to_db(back->conn_with_db, cmd->cmd);
    if (res == WRITE_OPER_RES) {
        conn->proc = process_read_db;
        conn->status = READ_DB;
        move_from_active_to_wait(conn);

        stop_event(dbw.wthrd->l, conn->w_data->handle);
        start_event(dbw.wthrd->l, conn->r_data->handle);

        return WAIT_PROC;
    } else if (res == WAIT_OPER_RES) {
        move_from_active_to_wait(conn);
        return WAIT_PROC;
    } else  if (res == ERR_OPER_RES) {
        free_connection(dbw.backends->conn);
        abort();
    }
    return DEL_PROC;
}


/*
* We attempt to read data from the database connection.
* If successful, we move the corresponding connection
* (which initiated the database request) to the active queue.
* If the purpose of the query was CACHE_UPDATE,
* we update the data in the cache and notify the database worker's
* event loop via eventfd that we have finished using the PostgreSQL connection
*/
proc_status process_read_db(connection* conn) {
    backend* back = (backend*)conn->data;
    command_to_db* cmd = conn->w_data->data;
    int err;
    req_table* req;
    db_oper_res res = read_from_db(back->conn_with_db, cmd->table, &req);
    if (res == READ_OPER_RES) {
        back->is_free = true;
        move_from_active_to_wait(conn);
        conn->proc = notify_db;
        conn->status = NOTIFY_DB;
        move_from_wait_to_active(cmd->conn);

        cmd = conn->w_data->data;
        event_notify(cmd->conn->wthrd->not);

        if (cmd->reason == CACHE_UPDATE) {
            char* key = cmd->key;
            int key_size = cmd->key_size;
            cache_data* data = init_cache_data(key, key_size, req);
            set_cache(data);
            free_cache_data(data);
        }

        free_db_command(cmd);

        err = pthread_spin_lock(dbw.lock);

        if (err != 0) {
            ereport(INFO, errmsg("process_read_db: pthread_spin_lock %s", strerror(err)));
            abort();
        }

        event_notify(conn->wthrd->not);

        err = pthread_spin_unlock(dbw.lock);
        if (err != 0) {
            ereport(INFO, errmsg("process_read_db: pthread_spin_unlock %s", strerror(err)));
            abort();
        }
        stop_event(dbw.wthrd->l, conn->r_data->handle);
        return WAIT_PROC;
    } else if (res == WAIT_OPER_RES) {
        move_from_active_to_wait(conn);
        return WAIT_PROC;
    } else  if (res == ERR_OPER_RES) {
        ereport(INFO, errmsg("process_read_db: res == ERR_OPER_RES"));
        abort();
    }
    return DEL_PROC;
}

/*
* Notification to the loop occurs in two cases:
*
* If a new event appears in the request queue and
* there is a free backend available for interacting with PostgreSQL,
* we assign it to process this request.
* If no free backend is available,
* we sleep until a notification arrives that a backend has been freed.
* Then, we assign it new work to process the request, if any exists.
*/
proc_status notify_db(connection* conn) {

    not_status not_s = event_get_notify(conn->wthrd->not);
    if (not_s == NOT_TA) {
        return ALIVE_PROC;
    }

    if (dbw.commands->count_commands == 0) {
        move_from_active_to_wait(conn);
        return WAIT_PROC;
    }

    for (int i = 0; i < dbw.count_backends; ++i) {
        if (dbw.backends[i].is_free) {
            dbw.backends[i].is_free = false;
            move_from_wait_to_active(dbw.backends[i].conn);
            dbw.backends[i].conn->w_data->data = get_command();
            dbw.backends[i].conn->data = &(dbw.backends[i]);
            dbw.backends[i].conn->proc = process_write_db;
            dbw.backends[i].conn->status = WRITE_DB;

            start_event(dbw.wthrd->l, dbw.backends[i].conn->w_data->handle);
            break;
        }
    }

    move_from_active_to_wait(conn);
    return WAIT_PROC;
}

//Based on the pre-formed data information, we create data for the cache and add metadata.
cache_data* init_cache_data(char* key, int key_size, req_table* args) {
    cache_data* data = wcalloc(sizeof(cache_data));
    data->cache_data_size = sizeof(cache_data);

    data->key = wcalloc(key_size * sizeof(char));
    data->cache_data_size += key_size * sizeof(char);
    data->key_size = key_size;
    memcpy(data->key, key, key_size);

    data->v = wcalloc(sizeof(value));
    data->cache_data_size += sizeof(value);
    data->v->count_fields = args->count_fields;
    data->v->count_tuples = args->count_tuples;
    data->v->values = wcalloc(args->count_tuples * sizeof(attr*));
    data->cache_data_size += args->count_tuples * sizeof(attr*);
    for (int i = 0; i < args->count_tuples; ++i) {
        data->v->values[i] = wcalloc(args->count_fields * sizeof(attr));
        data->cache_data_size += args->count_fields * sizeof(attr);
        for (int j = 0; j < args->count_fields; ++j ) {
            int column_name_size;
            column* c = get_column_info(args->table, args->columns[i][j].column_name);
            attr* a;

            if (c == NULL) {
                ereport(INFO, errmsg("init_cache_data: can't get column %s in table %s ", args->columns[i][j].column_name, args->table));
                abort();
            }
            column_name_size = strlen(c->column_name) + 1;
            a = &(data->v->values[i][j]);
            a->type = c->type;
            a->column_name = wcalloc(column_name_size * sizeof(char));
            data->cache_data_size += column_name_size * sizeof(char);
            memcpy(a->column_name, args->columns[i][j].column_name, column_name_size);
            a->is_nullable = c->is_nullable;


            a->data = wcalloc(sizeof(db_data));
            data->cache_data_size += sizeof(db_data);
            switch (a->type) {
                case INT:
                    a->data->num = (int)strtol(args->columns[i][j].data, NULL, 10);
                    break;
                case STRING:
                    a->data->str.size = args->columns[i][j].data_size;
                    a->data->str.str = wcalloc(a->data->str.size * sizeof(char));
                    data->cache_data_size += a->data->str.size * sizeof(char);
                    memcpy(a->data->str.str, args->columns[i][j].data, a->data->str.size );
                    break;
            }
        }
    }
    return data;
}

void free_cache_data(cache_data* data) {
    free(data);
}

void* start_db_worker(void*) {
    while (true) {
        CHECK_FOR_INTERRUPTS();
        loop_run(dbw.wthrd->l);
        loop_step(dbw.wthrd);
    }
}

/*
* Initializing the database, creating a loop to track events,
* including setting up a connection with eventfd
* to monitor notifications about new tasks being added.
*/
void init_db_worker(void) {
    connection* efd_conn;
    int efd ;
    int err;
    pthread_t db_tid;

    dbw.count_backends = config.db_conf.count_backend;
    dbw.backends = wcalloc(dbw.count_backends * sizeof(backend));
    init_db(dbw.backends);

    dbw.commands = wcalloc(sizeof(list_command));
    dbw.commands->first = dbw.commands->last = NULL;

    dbw.wthrd = wcalloc(sizeof(wthread));
    init_wthread(dbw.wthrd);
    dbw.wthrd->l = init_loop();

    dbw.lock = wcalloc(sizeof(pthread_spinlock_t));
    err = pthread_spin_init(dbw.lock, PTHREAD_PROCESS_PRIVATE);
    if (err != 0) {
        ereport(INFO, errmsg("init_db_worker: pthread_spin_lock %s", strerror(err)));
        abort();
    }

    efd = create_ev_notify(dbw.wthrd);

    efd_conn = create_connection(efd, dbw.wthrd);
    efd_conn->proc = notify_db;
    efd_conn->status = NOTIFY_DB;

    init_event(efd_conn, efd_conn->r_data->handle, efd_conn->fd, EVENT_READ);
    add_wait(efd_conn);

    for (int i = 0; i < dbw.count_backends; ++i) {
        connection* db_conn = create_connection(dbw.backends[i].fd, dbw.wthrd);
        init_event(db_conn, db_conn->r_data->handle, db_conn->fd, EVENT_READ);
        init_event(db_conn, db_conn->w_data->handle, db_conn->fd, EVENT_WRITE);
        add_wait(db_conn);
        dbw.backends[i].conn = db_conn;
        dbw.backends[i].is_free = true;
    }

    start_event(dbw.wthrd->l,  efd_conn->r_data->handle);

    err = pthread_create(&(db_tid), NULL, start_db_worker, NULL);
    if (err) {
        ereport(INFO, errmsg("init_worker: pthread_create error %s", strerror(err)));
        abort();
    }
}
