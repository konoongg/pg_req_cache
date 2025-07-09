#ifndef RESP_C_H
#define RESP_C_H

/* RESP
    +<string>\r\n - Simple Strings
    -<error message>\r\n - Error
    :<integer>\r\n - Integers
    $<length>\r\n<data>\r\n - Bulk Strings
    *<number of elements>\r\n<elements> - Arrays ($-1\r\n - null values)
*/

#include "connection.h"
#include "storage_data.h"
#include "io.h"

typedef enum resp_type resp_type;
typedef struct resp_string_arg resp_string_arg;
typedef struct resp_bulk_string_arg resp_bulk_string_arg;
typedef struct resp_array_arg resp_array_arg;
typedef struct resp_int_arg resp_int_arg;
typedef union generic_resp_arg generic_resp_arg;
typedef struct default_resp_answer default_resp_answer;

void create_array_resp(answer* answ, cache_response* res);
void create_num_resp(answer* answ, int num);
void create_simple_string_resp(answer* answ, char* src);
void init_def_resp (void);
void create_err_resp(answer* answ, char* src);

struct default_resp_answer {
    answer ok;
    answer pong;
    answer replica_cant_modify;


    answer aof;
    answer save;
};

#endif