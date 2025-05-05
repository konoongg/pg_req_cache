#ifndef QUERY_CACHE_C
#define QUERY_CACHE_C

#include <pthread.h>

#include "db.h"
#include "event.h"

typedef enum com_reason com_reason;
typedef struct command_to_db command_to_db;
typedef struct db_worker db_worker;
typedef struct list_command list_command;

void register_command(key_info* key_i, char* req, connection* conn, com_reason reason);
void init_db_worker(void);

enum com_reason {
    CACHE_SYNC, // It is necessary to synchronize the state of the cache and the database.
    CACHE_UPDATE, // It is necessary to load data from the cache.
};

/*
* A structure describing a database query.
* It contains information about the query and the reason for the query,
*   as different actions need to be taken depending on the reason.
*/
struct command_to_db {
    char* cmd;
    key_info* key;
    char* table;
    com_reason reason;
    command_to_db* next;
    connection* conn;
};

struct list_command {
    command_to_db* first;
    command_to_db* last;
    int count_commands;
};

/*
* A structure describing the database worker.
* It contains a queue of commands to be executed,
* an event loop, and a pool of backends through
* which interaction with the database occurs.
*/
struct db_worker {
    backend* backends;
    int count_backends;
    list_command* commands;
    pthread_mutex_t* lock;
    wthread* wthrd;
};

#endif
