#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>

#include "postgres.h"
#include "utils/elog.h"

#include "alloc.h"
#include "connection.h"
#include "event.h"

void add(connection* conn, conn_list* list) ;
void conn_list_lock(wthread* wthrd);
void conn_list_unlock(wthread* wthrd);
void delete(connection* conn, conn_list* list);
void free_wthread(wthread* wthrd);
void init_notify(e_notify* not);
void not_close(e_notify* not);

// Adding an element to the event list.
void add(connection* conn, conn_list* list) {
    conn->next = NULL;
    conn->prev = NULL;
    if (list->first == NULL) {
        list->first = list->last = conn;
    } else {
        conn->prev = list->last;
        list->last->next = conn;
        list->last = conn;
    }
}

// Removes an event from the list.
void delete(connection* conn, conn_list* list) {
    assert(list->first);
    if (conn->prev == NULL) {
        list->first = conn->next;
    } else {
        conn->prev->next = conn->next;
    }

    if (conn->next == NULL) {
        list->last = conn->prev;
    } else {
        conn->next->prev = conn->prev;
    }
}


/*
* Adds an element to the list of active events.
* Locking is required because the database worker thread may also interact with this list.
* The same applies to the function for removing from the active list,
* functions working with the pending list, and functions for moving elements between lists.
*/
void add_active(connection* conn) {
    int err = pthread_spin_lock(conn->wthrd->lock);
    if (err != 0) {
        ereport(INFO, errmsg("add_active: pthread_spin_lock %s", strerror(err)));
        abort();
    }

    conn->is_wait = false;

    add(conn, conn->wthrd->active);
    conn->wthrd->active_size++;

    err = pthread_spin_unlock(conn->wthrd->lock);
    if (err != 0) {
        ereport(INFO, errmsg("add_active: pthread_spin_unlock %s", strerror(err)));
        abort();
    }
}

void delete_active(connection* conn) {
    int err = pthread_spin_lock(conn->wthrd->lock);
    if (err != 0) {
        ereport(INFO, errmsg("delete_active: pthread_spin_lock %s", strerror(err)));
        abort();
    }

    delete(conn, conn->wthrd->active);
    conn->wthrd->active_size--;

    err = pthread_spin_unlock(conn->wthrd->lock);
    if (err != 0) {
        ereport(INFO, errmsg("delete_active: pthread_spin_unlock %s", strerror(err)));
        abort();
    }
}

void add_wait(connection* conn) {

    int err = pthread_spin_lock(conn->wthrd->lock);
    if (err != 0) {
        ereport(INFO, errmsg("add_wait: pthread_spin_lock %s", strerror(err)));
        abort();
    }

    conn->is_wait = true;
    add(conn, conn->wthrd->wait);
    conn->wthrd->wait_size++;

    err = pthread_spin_unlock(conn->wthrd->lock);
    if (err != 0) {
        ereport(INFO, errmsg("add_wait: pthread_spin_unlock %s", strerror(err)));
        abort();
    }
}


void delete_wait(connection* conn) {

    int err = pthread_spin_lock(conn->wthrd->lock);
    if (err != 0) {
        ereport(INFO, errmsg("delete_wait: pthread_spin_lock %s", strerror(err)));
        abort();
    }

    delete(conn, conn->wthrd->wait);
    conn->wthrd->wait_size--;

    err = pthread_spin_unlock(conn->wthrd->lock);
    if (err != 0) {
        ereport(INFO, errmsg("delete_wait: pthread_spin_unlock %s", strerror(err)));
        abort();
    }
}

void move_from_active_to_wait(connection* conn) {
    int err = pthread_spin_lock(conn->wthrd->lock);
    if (err != 0) {
        ereport(INFO, errmsg("move_from_active_to_wait: pthread_spin_lock %s", strerror(err)));
        abort();
    }

    assert(!conn->is_wait);
    delete(conn, conn->wthrd->active);
    conn->wthrd->active_size--;
    conn->is_wait = true;
    add(conn, conn->wthrd->wait);
    conn->wthrd->wait_size++;

    err = pthread_spin_unlock(conn->wthrd->lock);
    if (err != 0) {
        ereport(INFO, errmsg("move_from_active_to_wait: pthread_spin_unlock %s", strerror(err)));
        abort();
    }
}


void move_from_wait_to_active(connection* conn) {
    int err = pthread_spin_lock(conn->wthrd->lock);
    if (err != 0) {
        ereport(INFO, errmsg("move_from_wait_to_active: pthread_spin_lock %s", strerror(err)));
        abort();
    }

    assert(conn->is_wait);
    delete(conn, conn->wthrd->wait);
    conn->wthrd->wait_size--;
    conn->is_wait = false;
    add(conn, conn->wthrd->active);
    conn->wthrd->active_size++;

    err = pthread_spin_unlock(conn->wthrd->lock);
    if (err != 0) {
        ereport(INFO, errmsg("move_from_wait_to_active: pthread_spin_unlock %s", strerror(err)));
        abort();
    }
}

connection* create_connection(int fd, wthread* wthrd) {
    event_data* r_data;
    event_data* w_data;
    connection* conn = (connection*)wcalloc(sizeof(connection));

    conn->fd = fd;

    w_data = (event_data*)wcalloc(sizeof(event_data));
    r_data = (event_data*)wcalloc(sizeof(event_data));

    r_data->handle = wcalloc(sizeof(handle));
    w_data->handle = wcalloc(sizeof(handle));

    conn->w_data = w_data;
    conn->r_data = r_data;

    conn->next = NULL;
    conn->prev = NULL;
    conn->wthrd = wthrd;
    return conn;
}

void free_connection(connection* conn) {
    event_data* r_data = (event_data*)conn->r_data;
    event_data* w_data = (event_data*)conn->w_data;

    r_data->free_data(r_data->data);
    w_data->free_data(w_data->data);

    if (close(conn->fd) == -1) {
        char* err_msg = strerror(errno);
        ereport(INFO, errmsg("process_close: close() failed: %s\n", err_msg));
        abort();
    }

    free(r_data->handle);
    free(w_data->handle);
    free(r_data);
    free(w_data);
    free(conn);
}

void init_wthread(wthread* wthrd) {
    int err;

    wthrd->active = wcalloc(sizeof(conn_list));
    wthrd->active->first = wthrd->active->last = NULL;
    wthrd->active_size = 0;
    wthrd->wait = wcalloc(sizeof(conn_list));
    wthrd->wait->first = wthrd->wait->last = NULL;
    wthrd->wait_size = 0;

    wthrd->lock = wcalloc(sizeof(pthread_spinlock_t));

    err = pthread_spin_init(wthrd->lock, PTHREAD_PROCESS_PRIVATE);
    if (err != 0) {
        ereport(INFO, errmsg("init_wthread: pthread_spin_lock %s", strerror(err)));
        abort();
    }
}

void conn_list_lock(wthread* wthrd) {
    int err = pthread_spin_lock(wthrd->lock);
    if (err != 0) {
        ereport(INFO, errmsg("conn_list_lock: pthread_spin_lock %s", strerror(err)));
        abort();
    }
}

void conn_list_unlock(wthread* wthrd) {
    int err = pthread_spin_unlock(wthrd->lock);
    if (err != 0) {
        ereport(INFO, errmsg("conn_list_unlock: pthread_spin_unlock %s", strerror(err)));
        abort();
    }
}


void loop_step(wthread* wthrd) {
    while (wthrd->active_size != 0) {
        connection* cur_conn;

        conn_list_lock(wthrd);
        cur_conn = wthrd->active->first;
        conn_list_unlock(wthrd);

        while (cur_conn != NULL) {
            connection* cur_conn_next;

            conn_list_lock(wthrd);
            cur_conn_next = cur_conn->next;
            conn_list_unlock(wthrd);

            assert(!cur_conn->is_wait);
            cur_conn->proc(cur_conn);
            cur_conn = cur_conn_next;
        }
    }
}

void not_close(e_notify* not) {
    close(not->pipe_fd[0]);
    close(not->pipe_fd[1]);
    free(not);
}

void free_wthread(wthread* wthrd) {
    not_close(wthrd->not);
    close(wthrd->listen_socket);
    free(wthrd->active);
    free(wthrd->wait);
    loop_destroy(wthrd->l);
}

void finish_connection(connection* conn) {
    stop_event(conn->wthrd->l, conn->r_data->handle);
    stop_event(conn->wthrd->l, conn->w_data->handle);
    free_connection(conn);
}

void event_notify(e_notify* not) {
    while (true) {
        char sig_ev = 0;
        int res = write(not->pipe_fd[1], &sig_ev, 1);
        if (res == -1) {
            char* err = strerror(errno);
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            ereport(INFO, errmsg("event_notify fd %d: write %s", not->pipe_fd[1], err));
            abort();
        }
        break;
    }
}

not_status event_get_notify(e_notify* not) {
    char code;
    int res = read(not->pipe_fd[0], &code, 1);
    if (res < 0 && res != EAGAIN) {
        char* err = strerror(errno);
        ereport(INFO, errmsg("notify: read error %s", err));
        abort();
    } else if ( res == EAGAIN) {
        return NOT_TA;
    }
    return NOT_OK;
}

int create_ev_notify(wthread* wthrd) {
    int err;

    wthrd->not = wcalloc(sizeof(e_notify));
    err = pipe(wthrd->not->pipe_fd);
    if (err == -1) {
        ereport(INFO, errmsg("init_notify: pipe error %s", strerror(err)));
        abort();
    }
    return wthrd->not->pipe_fd[0];
}

