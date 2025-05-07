#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/eventfd.h>

#include "postgres.h"
#include "utils/elog.h"
#include "miscadmin.h"

#include "alloc.h"
#include "connection.h"
#include "cache_serializer.h"
#include "db.h"
#include "hash.h"
#include "query_cache_controller.h"

db_worker dbw;
extern config_redis config;

command_to_db* get_command(void);
proc_status notify_db(connection* conn);
proc_status process_read_db(connection* conn);
proc_status process_write_db(connection* conn);
void dbw_lock(void);
void dbw_unlock(void);
void free_db_command(command_to_db* cmd);
void* start_db_worker(void*);

void free_db_command(command_to_db* cmd) {
    destroy_key_info(cmd->key);
    free(cmd->table);
    free(cmd->cmd);
    free(cmd);
}

void dbw_lock(void) {
    int err = pthread_mutex_lock(dbw.lock);
    if (err != 0) {
        printf("dbw_lock: pthread_mutex_lock() failed: %s\n", strerror(err));
        abort();
    }
}


void dbw_unlock(void) {
    int err = pthread_mutex_unlock(dbw.lock);
    if (err != 0) {
        printf("dbw_lock: pthread_mutex_unlock() failed: %s\n", strerror(err));
        abort();
    }
}

/*
* Registering a new event for database processing.
* The event is added to the processing queue,
* and the database worker's loop is notified via eventfd that new events have arrived.
*/
void register_command(key_info* key_i, char* table, int table_size, char* req, connection* conn, com_reason reason) {
    command_to_db* cmd = wcalloc(sizeof(command_to_db));

    cmd->next = NULL;
    cmd->conn = conn;
    cmd->key = key_i;
    cmd->reason = reason;
    cmd->cmd = req;

    cmd->table = wcalloc(table_size * sizeof(char));
    memcpy(cmd->table, table, (table_size + 1));
    cmd->table[table_size] = '\0';

    dbw_lock();

    if (dbw.commands->first == NULL) {
        dbw.commands->first = dbw.commands->last = cmd;
    } else {
        dbw.commands->last->next = cmd;
        dbw.commands->last = dbw.commands->last->next;
    }
    dbw.commands->last->next = NULL;
    dbw.commands->count_commands++;
    assert(dbw.commands->count_commands > 0);
    event_notify(dbw.wthrd->not);

    dbw_unlock();
}

// Retrieving a command for processing from the queue
command_to_db* get_command(void) {
    command_to_db* cmd;

    dbw_lock();

    if (dbw.commands->first == NULL) {
        dbw_unlock();
        return NULL;
    }

    cmd = dbw.commands->first;

    ereport(INFO, errmsg("process_read_db: dbw.commands->first %p dbw.commands->first->next %p", dbw.commands->first, dbw.commands->first->next));
    dbw.commands->first = dbw.commands->first->next;
    dbw.commands->count_commands--;
    assert(dbw.commands->count_commands >= 0);
    if (dbw.commands->count_commands == 0) {
        dbw.commands->first = dbw.commands->last = NULL;
    }

    dbw_unlock();

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
    created_cache_respons* res;
    db_oper_res result = read_from_db(back->conn_with_db, cmd->table, &res);
    if (result == READ_OPER_RES) {
        move_from_wait_to_active(cmd->conn);

        cmd = conn->w_data->data;

        ereport(INFO, errmsg("process_read_db: event_notify %d", cmd->conn->wthrd->not->pipe_fd[0]));
        event_notify(cmd->conn->wthrd->not);
        //ereport(INFO, errmsg("process_read_db: READ_OPER_RES"));
        if (cmd->reason == CACHE_UPDATE) {
            //ereport(INFO, errmsg("process_read_db: CACHE_UPDATE"));
            set_cache(cmd->key, res->res, res->size );
            //ereport(INFO, errmsg("process_read_db: CACHE_UPDATE FINISH"));
        }
        free_db_command(cmd);
        stop_event(dbw.wthrd->l, conn->r_data->handle);

        conn->w_data->data = get_command();
        ereport(INFO, errmsg("process_read_db: conn->w_data->data %p", conn->w_data->data));
        if (conn->w_data->data == NULL) {
            ereport(INFO, errmsg("process_read_db: conn->w_data->data == NULL"));
            back->is_free = true;
            conn->proc = notify_db;
            conn->status = NOTIFY_DB;
            move_from_active_to_wait(conn);
            return WAIT_PROC;
        }

        conn->proc = process_write_db;
        conn->status = WRITE_DB;
        start_event(dbw.wthrd->l, conn->w_data->handle);
        return ALIVE_PROC;

    } else if (result == WAIT_OPER_RES) {
        move_from_active_to_wait(conn);
        return WAIT_PROC;
    } else  if (result == ERR_OPER_RES) {
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
    ereport(INFO, errmsg("notify_db: start"));

    not_status not_s = event_get_notify(conn->wthrd->not);
    if (not_s == NOT_TA) {
        return ALIVE_PROC;
    }

    dbw_lock();
    ereport(INFO, errmsg("notify_db: dbw.commands->count_commands  %d", dbw.commands->count_commands ));
    if (dbw.commands->count_commands == 0) {
        move_from_active_to_wait(conn);
        return WAIT_PROC;
    }
    dbw_unlock();


    for (int i = 0; i < dbw.count_backends; ++i) {

        ereport(INFO, errmsg("notify_db: dbw.backends[%d].is_free %d", i, dbw.backends[i].is_free));
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

    dbw.lock = wcalloc(sizeof(pthread_mutex_t));
    err = pthread_mutex_init(dbw.lock, NULL);
    if (err != 0){
        //ereport(INFO, errmsg("init_db_worker: pthread_mutex_init %s", strerror(err)));
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
        //ereport(INFO, errmsg("init_worker: pthread_create error %s", strerror(err)));
        abort();
    }
}
