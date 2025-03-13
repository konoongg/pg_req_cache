#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "postgres.h"
#include "utils/elog.h"

#include "alloc.h"
#include "cache.h"
#include "command_processor.h"
#include "connection.h"
#include "hash.h"
#include "io.h"
#include "pg_req_creater.h"
#include "query_cache_controller.h"
#include "resp_creater.h"

extern config_redis config;
extern default_resp_answer def_resp;
command_dict* com_dict;

char* get_column(char* key);
char* get_table_name(char* key);
process_result do_del(client_req* cl_req, answer* answ, connection* conn);
process_result do_get(client_req* cl_req, answer* answ, connection* conn);
process_result do_ping(client_req* cl_req, answer* answ, connection* conn);
process_result do_set(client_req* cl_req, answer* answ, connection* conn);
void free_command(int hash);

// A structure mapping command names to the functions that execute them.
redis_command commands[] = {
    {"del", do_del},
    {"get", do_get},
    {"set", do_set},
    {"ping", do_ping}
};

// Extracting the table name from the key.
char* get_table_name(char* key) {
    char* dot_position = strchr(key, '.');

    if (dot_position != NULL) {
        int length = dot_position - key;
        char* table_name = wcalloc((length + 1) * sizeof(char));
        memcpy(table_name, key, length);
        table_name[length] = '\0';
        return table_name;
    } else {
        return NULL;
    }
}

char* get_column(char* key) {
    char* dot_position_s;
    char* dot_position_f;
    int length;
    char* table_column;

    dot_position_f = strchr(key, '.');
    if (dot_position_f == NULL) {
        return NULL;
    }
    dot_position_s = strchr(dot_position_f + 1, '.');
    if (dot_position_s == NULL) {
        return NULL;
    }

    length = dot_position_s - dot_position_f - 1;
    table_column = wcalloc((length + 1) * sizeof(char));
    memcpy(table_column, dot_position_f + 1, length);
    table_column[length] = '\0';
    return table_column;
}


//In the case of receiving a PING command, send PONG back to the user.
process_result do_ping(client_req* req, answer* answ, connection* conn) {
    answ->answer_size = def_resp.pong.answer_size;
    answ->answer = wcalloc(answ->answer_size  * sizeof(char));
    memcpy(answ->answer, def_resp.pong.answer, answ->answer_size);
    return DONE;
}

/*
* Handling the GET command: by key, we access the cache.
* If the data is successfully retrieved,
* it is converted into RESP format and sent back.
* If the data is not found,
* we register a DB worker to fetch the data from the database
* and return a code indicating that the client needs to wait for the data to be retrieved.
*/
process_result do_get(client_req* cl_req, answer* answ, connection* conn) {
    char* key = cl_req->argv[1];
    int key_size = cl_req->argv_size[1];
    value* v = get_cache(key, key_size);

    if (v == NULL) {
        char* table_name = get_table_name(key);
        char* req_to_db = create_pg_get(key, key_size);
        move_from_active_to_wait(conn);
        register_command(table_name, req_to_db, conn, CACHE_UPDATE, key, key_size);

        return DB_REQ;
    }

    create_array_resp(answ, v);
    free_values(v);
    return DONE;
}


/*
* Handling the SET command:
* based on the received request,
* new data for the cache is generated.
* The data is updated in the cache,
* and an event is registered to update the data in the database.
*/
process_result do_set(client_req* cl_req, answer* answ, connection* conn) {
    char* key = cl_req->argv[1];
    char* value = cl_req->argv[2];
    int key_size = cl_req->argv_size[1];
    int value_size = cl_req->argv_size[2];
    char* key_column;
    char* req_to_db;

    req_table* new_req  = create_req_by_resp(value, value_size);
    cache_data* data;

    new_req->table = get_table_name(key);
    key_column = get_column(key);
    data = init_cache_data(key, key_size, new_req);

    req_to_db = create_pg_set(new_req->table, key_column, data);
    set_cache(data);

    answ->answer_size = def_resp.ok.answer_size;
    answ->answer = wcalloc(answ->answer_size  * sizeof(char));
    memcpy(answ->answer, def_resp.ok.answer, answ->answer_size);

    move_from_active_to_wait(conn);

    register_command(new_req->table, req_to_db, conn, CACHE_SYNC, key, key_size);

    free_req(new_req);
    free_cache_data(data);
    return DB_APPROVE;
}

/*
* Handling the DEL command:
* Each provided key is removed from the cache,
* and then an event is registered to delete the data from the database.
*/
process_result do_del(client_req* cl_req, answer* answ, connection* conn) {
    char** del_keys = cl_req->argv + 1;
    char* key = cl_req->argv[1];
    char* req_to_db;
    char* table_name;
    int count_del_keys = cl_req->argc - 1;
    int count_del = 0;
    int key_size = cl_req->argv_size[1];
    int* size_del_keys = cl_req->argv_size + 1;

    for (int i = 1; i < cl_req->argc; ++i) {
        char* del_key = cl_req->argv[i];
        int del_key_size = cl_req->argv_size[i];
        count_del += delete_cache(del_key, del_key_size);
    }

    move_from_active_to_wait(conn);

    table_name = get_table_name(key);
    req_to_db = create_pg_del(count_del_keys, del_keys, size_del_keys);
    register_command(table_name, req_to_db, conn, CACHE_SYNC, key, key_size);

    create_num_resp(answ, count_del);
    return DB_APPROVE;
}

void free_command(int hash) {
    command_entry* cur_entry = com_dict->commands[hash]->first;

    while (cur_entry != NULL) {
        command_entry* new_entry = cur_entry->next;
        free(cur_entry);
        cur_entry = new_entry;
    }

    free(com_dict->commands[hash]);
}


/*
* The initialization of callback commands is taking place.
* Initially, a mapping between the command name and the function to be called is stored.
* A hash table is populated to establish this correspondence.
*/
void init_commands(void) {
    com_dict = wcalloc(sizeof(command_dict));
    com_dict->hash_func = hash_pow_31_mod_100;
    com_dict->commands = wcalloc(HASH_P_31_M_100_SIZE * sizeof(entris*));

    for (int i = 0; i < COMMAND_DICT_SIZE; ++i) {
        int hash = com_dict->hash_func(commands[i].name);

        if (com_dict->commands[hash] == NULL) {
            com_dict->commands[hash] = wcalloc(sizeof(entris));
            com_dict->commands[hash]->first = com_dict->commands[hash]->last = wcalloc(sizeof(command_entry));
        } else {
            com_dict->commands[hash]->last->next = wcalloc(sizeof(command_entry));
        }
        com_dict->commands[hash]->last->next = NULL;
        com_dict->commands[hash]->last->command = &(commands[i]);
    }
}

// The submitted command is identified, and the corresponding function is invoked.
process_result process_command(client_req* req, answer* answ, connection* conn) {
    command_entry* cur_command;
    int hash;
    int size_command_name;

    hash = com_dict->hash_func(req->argv[0]);
    size_command_name = strlen(req->argv[0]) + 1;
    cur_command = com_dict->commands[hash]->first;
    while (cur_command != NULL) {
        if (strncmp(cur_command->command->name, req->argv[0], size_command_name) == 0) {
            return cur_command->command->func(req, answ, conn);
        }
        cur_command = cur_command->next;
    }
    return PROCESS_ERR;
}
