#ifndef C_PROCESSOR_H
#define C_PROCESSOR_H

#include <stdint.h>

#include "connection.h"
#include "io.h"

#define COMMAND_DICT_SIZE 5

typedef enum process_result process_result;
typedef struct command_dict command_dict;
typedef struct command_entry command_entry;
typedef struct entris entris;
typedef struct pg_data pg_data;
typedef struct redis_command redis_command;
typedef struct tuple_list tuple_list;

process_result process_command(client_req* req, answer* answ, connection* conn);
void free_resp_answ(void* ptr);
void init_commands(void);

struct redis_command {
    char* name;
    process_result (*func)(client_req* req, answer* answ, connection* conn);
};

struct command_dict {
    entris** commands;
    int (*hash_func)(char* key);
};

struct command_entry {
    redis_command* command;
    command_entry* next;
};

struct entris {
    command_entry* first;
    command_entry* last;
};


/*
* Results of request processing:
*
*   DONE: The command has been executed, and further operations can proceed.
*
*   PROCESS_ERR: An error occurred that needs to be handled.
*
*   DB_REQ: During the execution of the command, a request to PostgreSQL was required,
*   and it is necessary to wait for its response.
*/
enum process_result {
    DB_APPROVE,
    DB_REQ,
    DONE,
    PROCESS_ERR,
};

#endif
