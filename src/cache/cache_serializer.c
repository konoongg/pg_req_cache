#include <stdlib.h>

#include "postgres.h"

#include "libpq-fe.h"

#include "alloc.h"
#include "cache_serializer.h"
#include "logger.h"
#include "meta_db.h"
#include "parse_pg_command.h"
#include "storage_data.h"

extern config_cache config;

typedef struct key_info key_info;

static created_cache_respons* init_meta_ccr(int count_column, int count_tiuple) {
    created_cache_respons* ccr = wcalloc(sizeof(created_cache_respons));
    cache_response* res = shalloc(sizeof(cache_response));
    ccr->size = sizeof(cache_response);
    ccr->res = res;

    res->count_fields = count_column;
    res->count_tuples = count_tiuple;

    res->values = shalloc(res->count_tuples * sizeof(cache_attr*));
    ccr->size += res->count_tuples * sizeof(cache_attr*);

    (res->values)[0] = shalloc(res->count_fields * sizeof(cache_attr));
    ccr->size += res->count_fields * sizeof(cache_attr);
    res->columns = shalloc(res->count_fields  * sizeof(char*));
    ccr->size += res->count_fields * sizeof(char*);

    return ccr;
}

created_cache_respons* create_response_by_resp(char* table, char* value, int value_size) {
    created_cache_respons* ccr;
    cache_response* res;
    int start_pos;
    int cur_count_attr;
    int count_fields = 1;

    for (int i = 0; i < value_size; ++i) {
        if (value[i] == config.p_conf.delim) {
            count_fields++;
        }
    }
    ccr = init_meta_ccr(count_fields, 1);
    res = ccr->res;

    start_pos = 0;
    cur_count_attr = 0;
    for (int cur_pos = 0; cur_pos < value_size + 1; ++cur_pos) {
        if (value[cur_pos] == config.p_conf.delim || value[cur_pos] == '\0') {
            int index_delim;
            int attr_name_size;
            int attr_size;
            char* column_name;
            char* data;

            for (index_delim = start_pos; index_delim < cur_pos; ++index_delim) {
                if (value[index_delim] == ':') {
                    break;
                }
            }

            attr_name_size = index_delim - start_pos;
            attr_size = cur_pos - index_delim - 1;

            column_name = wcalloc((attr_name_size + 1)  * sizeof(char));
            memcpy(column_name, value + start_pos, attr_name_size);
            column_name[attr_name_size] = '\0';
            res->columns[cur_count_attr] = get_column_info(table, column_name);
            if (res->columns[cur_count_attr] == NULL) {
                cache_log(CACHE_ERROR, "create_response_by_resp: table: %s coulumn %s not found", table, column_name);
            }
            free(column_name);

            data = wcalloc((attr_size + 1) * sizeof(char));
            memcpy(data, value + index_delim + 1, attr_size);
            data[attr_size] = '\0';

            res->values[0][cur_count_attr].data = shalloc(sizeof(db_data));
            ccr->size += sizeof(db_data);
            switch (res->columns[cur_count_attr]->type) {
                case INT:
                    res->values[0][cur_count_attr].data->num = (int)strtol(data, NULL, 10);
                    break;
                case STRING:
                    res->values[0][cur_count_attr].data->str.size = attr_size;
                    res->values[0][cur_count_attr].data->str.str = shalloc(attr_size * sizeof(char));
                    ccr->size += res->values[0][cur_count_attr].data->str.size * sizeof(char);
                    memcpy(res->values[0][cur_count_attr].data->str.str, data, attr_size);
                    break;
            }
            free(data);
            cur_count_attr++;
            start_pos = cur_pos + 1;
        }
    }

    res->prepare_answer_valid = false;
    res->updated = false;
    res->prepare_answer_size = 0;

    return ccr;
}


// Creating a structure describing the cached data based on data received from the database.
created_cache_respons* create_response_by_pg(PGresult* result, char* table) {
    created_cache_respons* ccr = init_meta_ccr(PQnfields(result), PQntuples(result));
    cache_response* res = ccr->res;

    for (int column = 0; column < res->count_fields; ++column) {
        char* column_name = PQfname(result, column);
        if (column_name == NULL) {
            shfree(res->values);
            shfree(res->columns);
            shfree(res);
            return NULL;
        }
        res->columns[column] = get_column_info(table, column_name);
        if (res->columns[column] == NULL) {
            cache_log(CACHE_ERROR, "create_response_by_resp: table: %s coulumn %s not found", table, column_name);
        }
    }

    for (int row = 0; row < res->count_tuples; ++row) {
        res->values[row] = shalloc(res->count_fields * sizeof(cache_attr));
        ccr->size += res->count_fields * sizeof(cache_attr);
        for (int column = 0; column < res->count_fields; ++column) {
            char* value;
            int value_size;

            value = PQgetvalue(result, row, column);
            if (value == NULL) {
                for (int del_row = row; del_row >= row; del_row--) {
                    shfree(res->values[del_row]);
                }
                shfree(res->columns);
                shfree(res->values);
                shfree(res);
                return NULL;
            }

            value_size = PQgetlength(result, row, column);
            if (value_size == 0) {
                for (int del_row = row; del_row >= row; del_row--) {
                    shfree(res->values[del_row]);
                }
                shfree(res->columns);
                shfree(res->values);
                shfree(res);
                return NULL;
            }

            res->values[row][column].data = shalloc(sizeof(db_data));
            ccr->size += sizeof(db_data);
            switch (res->columns[column]->type) {
                case INT:
                    res->values[row][column].data->num = (int)strtol(value, NULL, 10);
                    break;
                case STRING:
                    res->values[row][column].data->str.size = value_size;
                    res->values[row][column].data->str.str = shalloc(value_size * sizeof(char));
                    ccr->size += res->values[row][column].data->str.size * sizeof(char);
                    memcpy(res->values[row][column].data->str.str, value, value_size);
                    break;
            }
        }
    }

    res->prepare_answer_valid = false;
    res->updated = false;
    res->prepare_answer_size = 0;

    return ccr;
}


created_cache_respons* create_response_by_pg_command(pg_parse_data* req) {
    table* t = get_table_info(req->table_name);
    created_cache_respons* ccr = init_meta_ccr(t->count_column, 1);
    cache_response* res = ccr->res;

    res->values[0] = shalloc(res->count_fields * sizeof(cache_attr));
    for (int i = 0; i < req->count_pars_column; ++i) {
        column* c = req->columns[i];
        int c_index = get_column_index(c->column_name, req->table_name);

        if (c_index == -1) {
            cache_log(CACHE_ERROR, "create_response_by_pg_command: can't find column %s in table %s", c->column_name, req->table_name);
        }

        res->columns[c_index] = c;

        switch (c->type) {
            case INT:
                res->values[0][c_index].data->num = (int)strtol(req->value[i], NULL, 10);
                break;
            case STRING:
                res->values[0][c_index].data->str.size = req->value_size[i];
                res->values[0][c_index].data->str.str = shalloc(req->value_size[i] * sizeof(char));
                ccr->size += res->values[0][c_index].data->str.size * sizeof(char);
                memcpy(res->values[0][c_index].data->str.str, req->value, req->value_size[i]);
                break;
        }
    }

    res->prepare_answer_valid = false;
    res->updated = false;
    res->prepare_answer_size = 0;

    return ccr;
}

key_info* create_key_info(char* key, int key_size) {
    key_info* key_i = wcalloc(sizeof(key_info));

    char* dot_position_s;
    char* dot_position_f;

    char* key_start = key;
    int flag_size = 0;
    key_i->direct = false;

    if (key[0] == '[') {
        for (int i = 0; i < key_size; ++i) {
            if (key[i] == ']') {
                key_start = key + i + 1;
                flag_size = i + 1;
            }
            switch (key[i]) {
                case 'D':
                    key[i] = 'N';
                    key_i->direct = true;
                    break;
            }
        }
    }

    dot_position_f = strchr(key_start, '.');
    if (dot_position_f == NULL) {
        return NULL;
    }

    dot_position_s = strchr(dot_position_f + 1, '.');
    if (dot_position_s == NULL) {
        return NULL;
    }

    key_i->full_key = key_start;
    key_i->full_size = key_size - flag_size;

    key_i->table_size = dot_position_f - key_start;
    key_i->table = wcalloc((key_i->table_size + 1) * sizeof(char));
    memcpy(key_i->table, key_start, key_i->table_size);
    key_i->table[key_i->table_size] = '\0';

    key_i->table_column_size = dot_position_s - key_start;
    key_i->table_column = wcalloc((key_i->table_column_size + 1) * sizeof(char));
    memcpy(key_i->table_column, key_start, key_i->table_column_size);
    key_i->table_column[key_i->table_column_size] = '\0';

    key_i->column_size = dot_position_s - dot_position_f - 1;
    key_i->column = wcalloc((key_i->column_size + 1) * sizeof(char));
    memcpy(key_i->column, dot_position_f + 1, key_i->column_size);
    key_i->column[key_i->column_size] = '\0';

    key_i->value_size = (key + key_size) - dot_position_s - 1;
    key_i->value = wcalloc((key_i->value_size + 1) * sizeof(char));
    memcpy(key_i->value, dot_position_s + 1, key_i->value_size);
    key_i->value[key_i->value_size] = '\0';

    return key_i;
}

key_info* create_key_info_by_pg_command(pg_parse_data* req) {
    key_info* key_i = wcalloc(sizeof(key_info));
    column* c;
    int offset = 0;

    key_i->direct = false;

    key_i->table_size = strlen(req->table_name);
    key_i->table = wcalloc((key_i->table_size + 1) * sizeof(char));
    memcpy(key_i->table, req->table_name, key_i->table_size);
    key_i->table[key_i->table_size] = '\0';

    c = req->key_column;

    if (c == NULL) {
        cache_log(CACHE_ERROR, "create_key_info_by_record: wrong table name %s - can't find key column", req->table_name);
    }

    key_i->column_size = strlen(c->column_name);
    key_i->column = wcalloc((key_i->column_size + 1) * sizeof(char));
    memcpy(key_i->column, c->column_name, key_i->column_size);
    key_i->column[key_i->column_size] = '\0';

    key_i->value_size = req->key_value_size;
    key_i->value = wcalloc((key_i->value_size + 1) * sizeof(char));
    memcpy(key_i->value, req->columns, key_i->column_size);
    key_i->value[key_i->value_size] = '\0';

    key_i->table_column_size = key_i->table_size + 1 + key_i->column_size;
    key_i->table_column = wcalloc(sizeof(key_i->table_column_size + 1) * sizeof(char));
    memcpy(key_i->table_column, req->table_name, key_i->table_size);
    offset = key_i->table_size;
    key_i->table_column[offset] = '.';
    offset++;
    memcpy(key_i->table_column + offset , c->column_name,  key_i->column_size);
    key_i->table_column[key_i->table_column_size] = '\0';

    offset = 0;
    key_i->full_size = key_i->table_size + 1 + key_i->column_size + 1 + key_i->value_size;
    key_i->full_key = wcalloc((key_i->full_size+ 1) * sizeof(char));
    memcpy(key_i->full_key, req->table_name, key_i->table_size);
    offset += key_i->table_size;
    key_i->full_key[offset] = '.';
    offset++;
    memcpy(key_i->full_key + offset , c->column_name,  key_i->column_size);
    offset += key_i->column_size;
    key_i->full_key[offset] = '.';
    offset++;
    memcpy(key_i->full_key + offset , key_i->value,  key_i->value_size);
    key_i->full_key[key_i->full_size] = '\0';

    return key_i;
}

void destroy_key_info(key_info* key_i) {
    if (key_i == NULL) {
        return;
    }

    free(key_i->column);
    free(key_i->table_column);
    free(key_i->table);
    free(key_i->value);
    free(key_i);
}