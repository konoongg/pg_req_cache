#ifndef PG_PARSER_H
#define PG_PARSER_H

#include "parse_pg_command.h"
#include "storage_data.h"

typedef enum pg_command_type pg_command_type;
typedef struct pg_parse_data pg_parse_data;

#define UPDATE_SKIP_SIZE 7
#define SET_SKIP_SIZE 4
#define WHERE_SKIP_SIZE 6

pg_parse_data* parse_update(const char* command);
void destroy_parse_data(pg_parse_data* parse);

enum pg_command_type {
    PG_UPDATE,
    PG_DELETE,
};

struct pg_parse_data {
    pg_command_type command_t;
    char* table_name;
    column** columns;
    int count_pars_column;

    char** value;
    int* value_size;

    column* key_column;
    char* key_value;
    int key_value_size;
};

#endif