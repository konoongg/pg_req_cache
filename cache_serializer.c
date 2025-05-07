#include <stdlib.h>

#include "postgres.h"
#include "utils/elog.h"

#include "libpq-fe.h"

#include "alloc.h"
#include "cache_serializer.h"
#include "storage_data.h"
#include "db.h"

extern config_redis config;

typedef struct key_info key_info;

created_cache_respons* create_response_by_resp(char* table, char* value, int value_size) {
    created_cache_respons* ccr = wcalloc(sizeof(created_cache_respons));
    cache_response* res = wcalloc(sizeof(cache_response));
    int start_pos;
    int cur_count_attr;

    ccr->size = sizeof(cache_response);
    ccr->res = res;

    res->count_fields = 1;
    res->count_tuples = 1; // from resp only one
    for (int i = 0; i < value_size; ++i) {
        if (value[i] == config.p_conf.delim) {
            res->count_fields++;
        }
    }
    res->values = wcalloc(res->count_tuples * sizeof(cache_attr*));
    ccr->size += res->count_tuples * sizeof(cache_attr*);
    res->values[0] = wcalloc(res->count_fields * sizeof(cache_attr*));
    ccr->size += res->count_fields * sizeof(cache_attr);
    res->columns = wcalloc(res->count_fields  * sizeof(char*));
    ccr->size += res->count_fields * sizeof(char*);

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
                ereport(INFO, errmsg("create_response_by_resp: table: %s coulumn %s not found", table, column_name));
                abort();
            }
            free(column_name);

            data = wcalloc((attr_size + 1) * sizeof(char));
            memcpy(data, value + index_delim + 1, attr_size);
            data[attr_size] = '\0';

            res->values[0][cur_count_attr].data = wcalloc(sizeof(db_data));
            ccr->size += sizeof(db_data);
            switch (res->columns[cur_count_attr]->type) {
                case INT:
                    res->values[0][cur_count_attr].data->num = (int)strtol(data, NULL, 10);
                    break;
                case STRING:
                    res->values[0][cur_count_attr].data->str.size = attr_size;
                    res->values[0][cur_count_attr].data->str.str = wcalloc(attr_size * sizeof(char));
                    ccr->size += res->values[0][cur_count_attr].data->str.size * sizeof(char);
                    memcpy(res->values[0][cur_count_attr].data->str.str, data, attr_size);
                    break;
            }
            free(data);
            cur_count_attr++;
            start_pos = cur_pos + 1;
        }
    }
    return ccr;
}


// Creating a structure describing the cached data based on data received from the database.
created_cache_respons* create_response_by_pg(PGresult* result, char* table) {
    created_cache_respons* ccr = wcalloc(sizeof(created_cache_respons));
    cache_response* res = wcalloc(sizeof(cache_response));

    ccr->res = res;
    ccr->size = sizeof(cache_response);

    res->count_fields = PQnfields(result);
    res->count_tuples = PQntuples(result);
    res->values = wcalloc(res->count_tuples * sizeof(cache_attr*));
    res->columns = wcalloc(res->count_tuples * sizeof(column*));
    ccr->size += res->count_tuples * sizeof(cache_attr*);

    for (int column = 0; column < res->count_fields; ++column) {
        char* column_name = PQfname(result, column);
        if (column_name == NULL) {
            free(res->values);
            free(res->columns);
            free(res);
            return NULL;
        }
        res->columns[column] = get_column_info(table, column_name);
        if (res->columns[column] == NULL) {
            ereport(INFO, errmsg("create_response_by_resp: table: %s coulumn %s not found", table, column_name));
            abort();
        }
    }

    for (int row = 0; row < res->count_tuples; ++row) {
        res->values[row] = wcalloc(res->count_fields * sizeof(cache_attr));
        ccr->size += res->count_fields * sizeof(cache_attr);
        for (int column = 0; column < res->count_fields; ++column) {
            char* value;
            int value_size;

            value = PQgetvalue(result, row, column);
            if (value == NULL) {
                for (int del_row = row; del_row >= row; del_row--) {
                    free(res->values[del_row]);
                }
                free(res->columns);
                free(res->values);
                free(res);
                return NULL;
            }

            value_size = PQgetlength(result, row, column);
            if (value_size == 0) {
                for (int del_row = row; del_row >= row; del_row--) {
                    free(res->values[del_row]);
                }
                free(res->columns);
                free(res->values);
                free(res);
                return NULL;
            }

            res->values[row][column].data = wcalloc(sizeof(db_data));
            ccr->size += sizeof(db_data);
            switch (res->columns[column]->type) {
                case INT:
                    res->values[row][column].data->num = (int)strtol(value, NULL, 10);
                    break;
                case STRING:
                    res->values[row][column].data->str.size = value_size;
                    res->values[row][column].data->str.str = wcalloc(value_size * sizeof(char));
                    ccr->size += res->values[row][column].data->str.size * sizeof(char);
                    memcpy(res->values[row][column].data->str.str, value, value_size);
                    break;
            }
        }
    }
    return ccr;
}

key_info* create_key_info(char* key, int key_size) {
    key_info* key_i = wcalloc(sizeof(key_info));
    char* dot_position_s;
    char* dot_position_f;


    dot_position_f = strchr(key, '.');
    if (dot_position_f == NULL) {
        return NULL;
    }

    dot_position_s = strchr(dot_position_f + 1, '.');
    if (dot_position_s == NULL) {
        return NULL;
    }

    key_i->full_key = key;
    key_i->full_size = key_size;

    key_i->table_size = dot_position_f - key;
    key_i->table = wcalloc((key_i->table_size + 1) * sizeof(char));
    memcpy(key_i->table, key, key_i->table_size);
    key_i->table[key_i->table_size] = '\0';

    key_i->table_column_size = dot_position_s - key;
    key_i->table_column = wcalloc((key_i->table_column_size + 1) * sizeof(char));
    memcpy(key_i->table_column, key, key_i->table_column_size);
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