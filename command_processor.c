#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "postgres.h"
#include "utils/elog.h"

#include "alloc.h"
#include "cache_serializer.h"
#include "cache.h"
#include "command_processor.h"
#include "connection.h"
#include "hash.h"
#include "ht_response_type.h"
#include "ht.h"
#include "io.h"
#include "pg_req_creater.h"
#include "query_cache_controller.h"
#include "resp_creater.h"
#include "stats.h"

extern config_redis config;
extern default_resp_answer def_resp;
command_dict* com_dict;

process_result do_config(client_req* cl_req, answer* answ, connection* conn);
process_result do_del(client_req* cl_req, answer* answ, connection* conn);
process_result do_get(client_req* cl_req, answer* answ, connection* conn);
process_result do_info(client_req* req, answer* answ, connection* conn);
process_result do_ping(client_req* cl_req, answer* answ, connection* conn);
process_result do_set(client_req* cl_req, answer* answ, connection* conn);
void free_command(int hash);
void to_lower(char* word, int size);

// A structure mapping command names to the functions that execute them.
redis_command commands[] = {
    {"del", do_del},
    {"get", do_get},
    {"set", do_set},
    {"ping", do_ping},
    {"config", do_config},
    {"info", do_info}
};

process_result do_info(client_req* req, answer* answ, connection* conn) {
    answ->answer_size = def_resp.pong.answer_size;
    answ->answer = wcalloc(answ->answer_size  * sizeof(char));
    memcpy(answ->answer, def_resp.pong.answer, answ->answer_size);
    return DONE;
}

//In the case of receiving a PING command, send PONG back to the user.
process_result do_ping(client_req* req, answer* answ, connection* conn) {
    answ->answer_size = def_resp.pong.answer_size;
    answ->answer = wcalloc(answ->answer_size  * sizeof(char));
    memcpy(answ->answer, def_resp.pong.answer, answ->answer_size);
    return DONE;
}

process_result do_config(client_req* req, answer* answ, connection* conn) {
    to_lower(req->argv[1], req->argv_size[1]);
    if (strncmp("get", req->argv[1], 3) == 0 && req->argv_size[1] == 3) {
        if (strncmp("save", req->argv[2], 4) == 0 && req->argv_size[2] == 4) {
            answ->answer_size = def_resp.save.answer_size;
            answ->answer = wcalloc(answ->answer_size  * sizeof(char));
            memcpy(answ->answer, def_resp.save.answer, answ->answer_size);
        } else if (strncmp("appendonly", req->argv[2], 10) == 0 && req->argv_size[2] == 10) {
            answ->answer_size = def_resp.aof.answer_size;
            answ->answer = wcalloc(answ->answer_size  * sizeof(char));
            memcpy(answ->answer, def_resp.aof.answer, answ->answer_size);
        }
    }
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
    //ereport(INFO, errmsg("do_get: start"));
    char* key = cl_req->argv[1];
    int key_size = cl_req->argv_size[1];
    key_info* key_i = create_key_info(key, key_size);
    cache_response* res;
    bool expeted_prepare = true;
    //ereport(INFO, errmsg("do_get: get"));

    data_version* version = get_cache(key_i);
    //ereport(INFO, errmsg("do_get: finish get %p", res));
    if (version == NULL) {
        char* req_to_db;

        report_cache_miss();
        //ereport(INFO, errmsg("do_get: res == NULL table_size %d", key_i->table_size));
        req_to_db = create_pg_get(key_i);
        move_from_active_to_wait(conn);
        register_command(key_i, key_i->table, key_i->table_size, req_to_db, conn, CACHE_UPDATE);
        //ereport(INFO, errmsg("do_get: DB_REQ"));
        return DB_REQ;
    }

    res = version->value;

    destroy_key_info(key_i);
    if (atomic_compare_exchange_strong(&(res->prepare_answer_valid), &expeted_prepare, true)) {
        answ->answer_size = res->prepare_answer_size;
        answ->answer = wcalloc(res->prepare_answer_size * sizeof(char));
        memcpy(answ->answer, res->prepare_answer, answ->answer_size);
    } else {
        bool expected_not_update = false;
        create_array_resp(answ, res);
        if (atomic_compare_exchange_strong(&(res->updated), &expected_not_update, false)) {
            res->prepare_answer_size = answ->answer_size;
            res->prepare_answer = wcalloc(res->prepare_answer_size * sizeof(char));
            memcpy(res->prepare_answer, answ->answer, res->prepare_answer_size);
            atomic_store(&(res->prepare_answer_valid), true);
            atomic_store(&(res->updated), false);
        }
    }

    drop_version(version);
    //ereport(INFO, errmsg("do_get: DONE"));
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
    created_cache_respons* res;
    char* req_to_db;

    key_info* key_i = create_key_info(key, key_size);
    res = create_response_by_resp(key_i->table, value, value_size);
    req_to_db = create_pg_set(key_i, res->res);

    set_cache(key_i, res->res, res->size);

    answ->answer_size = def_resp.ok.answer_size;
    answ->answer = wcalloc(answ->answer_size  * sizeof(char));

    memcpy(answ->answer, def_resp.ok.answer, answ->answer_size);
    move_from_active_to_wait(conn);
    register_command(NULL, key_i->table, key_i->table_size, req_to_db, conn, CACHE_SYNC);
    destroy_key_info(key_i);
    return DB_APPROVE;
}

/*
* Handling the DEL command:
* Each provided key is removed from the cache,
* and then an event is registered to delete the data from the database.
*/
process_result do_del(client_req* cl_req, answer* answ, connection* conn) {
    char* req_to_db;
    int count_del_keys = cl_req->argc - 1;
    key_info** del_keys = wcalloc(count_del_keys * sizeof(key_info*));
    int count_del = 0;

    for (int i = 1; i < cl_req->argc; ++i) {
        del_keys[i - 1] = create_key_info(cl_req->argv[i], cl_req->argv_size[i]);
        count_del += delete_cache(del_keys[i - 1]);
    }

    move_from_active_to_wait(conn);

    req_to_db = create_pg_del(count_del_keys, del_keys);
    register_command(NULL, del_keys[0]->table, del_keys[0]->table_size, req_to_db, conn, CACHE_SYNC);

    for (int i = 0; i < count_del_keys; ++i) {
        destroy_key_info(del_keys[i]);
    }
    free(del_keys);
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

void to_lower(char* word, int size) {
    for (int i = 0; i < size; i++) {
        word[i] = tolower((unsigned char)word[i]);
    }
}

// The submitted command is identified, and the corresponding function is invoked.
process_result process_command(client_req* req, answer* answ, connection* conn) {
    command_entry* cur_command;
    int hash;
    int size_command_name;
    to_lower(req->argv[0], req->argv_size[0]);
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
