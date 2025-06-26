#ifndef CONNECTION_H
#define CONNECTION_H

#include <stdbool.h>

#include "event.h"

#define NOT_USE_FD -1

typedef enum not_status not_status;
typedef enum conn_status conn_status;
typedef enum proc_status proc_status;
typedef struct conn_list conn_list;
typedef struct connection connection;
typedef struct e_notify e_notify;
typedef struct event_data event_data;
typedef struct handle handle;
typedef struct wthread wthread;

connection* create_connection(int fd, wthread* wthrd);
int create_ev_notify(wthread* wthrd);
not_status event_get_notify(e_notify* not);
void add_active(connection* conn);
void add_wait(connection* conn);
void delete_active(connection* conn);
void delete_wait(connection* conn);
void event_notify(e_notify* not);
void finish_connection(connection* conn);
void free_connection(connection* conn);
void init_wthread(wthread* wthrd);
void loop_step(wthread* wthrd);
void move_from_active_to_wait(connection* conn);
void move_from_wait_to_active(connection* conn);

struct event_data {
    void* data;
    void (*free_data)(void* data);
    handle* handle;
};

enum conn_status {
    ACCEPT, // accept connect
    CLOSE, // close connect
    READ, // read data from connect
    WRITE, // write data to connect
    PROCESS, // proccess data
    NOTIFY, // Handling the notification that the active connections queue needs to be checked

    // Similar functions for the loop running within the database worker.
    NOTIFY_DB,
    READ_DB,
    WRITE_DB,

    // Similar functions for the loop running within the cache gc worker.
    CHECK_CACHE,
};

enum not_status {
    NOT_OK,
    NOT_TA, // try againt
};


/*
* Function execution status:
*
* ALIVE_PROC: Continue processing.
* DEL_PROC: Terminate the connection.
* WAIT_PROC: Await processing.
*/
enum proc_status {
    ALIVE_PROC,
    DEL_PROC,
    WAIT_PROC,
};

/*
* A structure describing a connection.
* It stores information related to receiving and sending data to the user,
* as well as the function to be called when this connection enters the active connections queue.
*/
struct connection {
    bool is_wait;
    conn_status status;
    connection* next;
    connection* prev;
    event_data* r_data;
    event_data* w_data;
    int fd;
    proc_status (*proc)(connection* conn);
    void* data; // not free data
    wthread* wthrd;
};

struct conn_list {
    connection* first;
    connection* last;
};

//A structure describing the execution loop, containing queues for active and pending connections.
struct wthread {
    int id;
    conn_list* active;
    conn_list* wait;
    event_loop* l;
    int active_size;
    e_notify* not;
    int listen_socket;
    int wait_size;
    pthread_spinlock_t* lock;
};

struct e_notify {
    int pipe_fd[2];
};

#endif
