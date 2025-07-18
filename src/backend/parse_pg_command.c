#include <stdbool.h>
#include <string.h>

#include "alloc.h"
#include "logger.h"
#include "meta_db.h"
#include "parse_pg_command.h"

#define NO_KEY false
#define KEY true

static void skip_space(char** command) {
    while (**command == ' ') {
        (*command)++;
    }
}

static void find_table_name(pg_parse_data* parse, char** command) {
    char* start_table_name;
    char* end_table_name;
    int table_name_size = 0;
    char* cur_char;

    skip_space(command);

    cur_char = *command;

    start_table_name = cur_char;

    while (*cur_char != ' ') {
        cur_char++;
    }
    end_table_name = cur_char;

    table_name_size = end_table_name - start_table_name;
    parse->table_name = wcalloc((table_name_size + 1) * sizeof(char));

    if (*start_table_name == '\"') {
        memcpy(parse->table_name, start_table_name + 1, table_name_size - 2); // remove ""
    } else {
        memcpy(parse->table_name, start_table_name, table_name_size);
    }

    parse->table_name[table_name_size] = '\0';
    skip_space(&cur_char);

    *command = cur_char;
}

static void find_column_value_pare(pg_parse_data* parse, char** command, int index, bool is_key) {
    char* cur_pos = *command;
    char* start_column_name = cur_pos;
    char* end_column_name;
    char* start_value;
    char* column_name;
    char* end_value;

    int column_name_size = 0;
    int value_size = 0;

    while (*cur_pos != ' ' && *cur_pos != '=') {
        cur_pos++;
    }

    end_column_name = cur_pos;
    column_name_size = end_column_name - start_column_name;

    column_name = wcalloc((column_name_size + 1) * sizeof(char));
    if (*start_column_name == '\"') {
        column_name_size -=2;
        start_column_name++;
    }

    memcpy(column_name, start_column_name, column_name_size);
    column_name[column_name_size] = '\0';

    skip_space(&cur_pos);
    cur_pos++; // skip =
    skip_space(&cur_pos);

    start_value = cur_pos;
    while (*cur_pos != ' ' && *cur_pos != 0) {
        cur_pos++;
    }
    end_value = cur_pos;
    value_size = end_value - start_value;

    if (*start_value = '\'') {
        start_value++;
        value_size -=2;
    }

    if (is_key) {
        parse->key_column = get_column_info(parse->table_name, column_name);
        parse->key_value = wcalloc((value_size + 1) * sizeof(char));
        memcpy(parse->key_value, start_value, value_size);
        parse->key_value[value_size] = '\0';
        parse->key_value_size = value_size;
    } else {
        parse->columns[index] = get_column_info(parse->table_name, column_name);
        parse->value[index] = wcalloc((value_size + 1) * sizeof(char));
        memcpy(parse->value[index], start_value, value_size);
        parse->value[index][value_size] = '\0';
        parse->value_size[index] = value_size;
    }

    free(column_name);
    skip_space(command);

    *command = cur_pos;
}

static void find_set_column (pg_parse_data* parse, char** command) {
    table* t = get_table_info(parse->table_name);

    if (t == NULL) {
        cache_log(CACHE_ERROR, "find_set_column: can't find table withe name %s", parse->table_name);
    }

    int max_count_column = t->count_column;
    int cur_index = 0;

    parse->columns = wcalloc(max_count_column * sizeof(column*));
    parse->value =  wcalloc(max_count_column * sizeof(char*));
    parse->value_size = wcalloc(max_count_column * sizeof(int));

    skip_space(command);

    do {
        find_column_value_pare(parse, command, cur_index, NO_KEY);
        cur_index++;
    } while (**command == ',');
    parse->count_pars_column = cur_index;

    skip_space(command);
}

static void find_key_column(pg_parse_data* parse, char** command) {
    skip_space(command);
    find_column_value_pare(parse, command, -1, KEY);
}

pg_parse_data* parse_update(const char* command) {
    pg_parse_data* parse = wcalloc(sizeof(pg_parse_data));
    char* cur_char = (char*)command;
    cur_char += UPDATE_SKIP_SIZE;
    find_table_name(parse, &cur_char);
    cur_char += SET_SKIP_SIZE;

    find_set_column(parse, &cur_char);

    cur_char += WHERE_SKIP_SIZE;

    find_key_column(parse, &cur_char);

    return parse;
}

void destroy_parse_data(pg_parse_data* parse) {
    for (int i = 0; i < parse->count_pars_column; ++i) {
        free(parse->value[0]);
    }

    free(parse->value_size);
    free(parse->value);
    free(parse->key_value);
    free(parse->columns);
    free(parse);
}