#include <stdlib.h>

#include "postgres.h"
#include "utils/elog.h"

#include "alloc.h"
#include "cache_serializer.h"
#include "pg_req_creater.h"

extern config_redis config;


char* create_pg_get(key_info* key_i){
    int size_req = SELECT_BASE_SIZE + key_i->column_size + key_i->table_size + key_i->value_size + 1; // \0(+1)
    char* bd_req = wcalloc(size_req * sizeof(char));
    snprintf(bd_req, size_req, "SELECT * FROM %s WHERE %s = '%s';", key_i->table, key_i->column, key_i->value);
    bd_req[size_req - 1] = '\0';
    return bd_req;
}

char* create_pg_del(int count, key_info** key_i) {
    char* bd_req;

    int del_cond_index = 0;
    int size_req = TRANSACTION_SIZE;
    for (int i = 0; i < count; ++i)  {
        size_req += key_i[i]->table_size + key_i[i]->column_size + key_i[i]->value_size + DELETE_BASE_SIZE;
    }

    bd_req = wcalloc(size_req * sizeof(char));
    memcpy(bd_req + del_cond_index, "BEGIN;", 6);
    del_cond_index += 6;
    for (int i = 0; i < count; ++i) {
        int size_del_req = DELETE_BASE_SIZE + key_i[i]->table_size + key_i[i]->column_size + key_i[i]->value_size;
        char* del_req = wcalloc(size_del_req * sizeof(char));
        snprintf(del_req, size_req, "DELETE FROM %s WHERE %s=\'%s\';", key_i[i]->table, key_i[i]->column, key_i[i]->value);
        memcpy(bd_req + del_cond_index, del_req, size_del_req);
        del_cond_index += size_del_req;
        free(del_req);
    }

    memcpy(bd_req + del_cond_index, "end;", 4);
    return bd_req;
}

char* create_pg_set(key_info* key_i, cache_response* data) {
    char* bd_req;
    int size_req = SET_BASE_SIZE;

    char* columns_name;
    char* columns_value;
    char* set_values;

    int columns_name_index = 0;
    int columns_value_index = 0;
    int set_values_index = 0;

    int columns_name_size = data->count_fields - 1 ; // count ','
    int columns_value_size = data->count_fields - 1; // count ','
    int set_values_size = data->count_fields - 1; // count ',';
    int count_attr = data->count_fields;

    for (int i = 0; i < count_attr; ++i) {
        columns_name_size += strlen(data->columns[i]->column_name);
        set_values_size += strlen(data->columns[i]->column_name) + 1; // +1 - =
        switch(data->columns[i]->type) {
            case STRING:
                string* str = (string*)data->values[0][i].data;
                columns_value_size += str->size + 2; // 'str'
                set_values_size += str->size + 2; // 'str'
                break;
            case INT:
                char str_num[MAX_STR_NUM_SIZE];
                int* num = (int*)data->values[0][i].data;
                snprintf(str_num, MAX_STR_NUM_SIZE, "%d", *num);
                set_values_size += strlen(str_num);
                columns_value_size += strlen(str_num);
                break;
        }
    }


    columns_name = wcalloc((columns_name_size + 1) * sizeof(char));
    columns_value = wcalloc( (columns_value_size + 1) * sizeof(char));
    set_values = wcalloc((set_values_size + 1) * sizeof(char));
    for (int i = 0; i < count_attr; ++i) {

        int c_name_size = strlen(data->columns[i]->column_name);
        memcpy(columns_name + columns_name_index, data->columns[i]->column_name, c_name_size);
        columns_name_index += c_name_size;

        memcpy(set_values + set_values_index, data->columns[i]->column_name, c_name_size);
        set_values_index += c_name_size;

        memcpy(set_values + set_values_index, "=", 1);
        set_values_index += 1;

        switch(data->columns[i]->type) {
            case STRING:
                string* str = (string*)data->values[0][i].data;
                memcpy(columns_value + columns_value_index, "\'", 1);
                columns_value_index += 1;

                memcpy(columns_value + columns_value_index, str->str, str->size);
                columns_value_index += str->size;

                memcpy(columns_value + columns_value_index, "\'", 1);
                columns_value_index += 1;

                memcpy(set_values + set_values_index, "\'", 1);
                set_values_index += 1;
                memcpy(set_values + set_values_index, str->str, str->size);
                set_values_index +=  str->size;

                memcpy(set_values + set_values_index, "\'", 1);
                set_values_index += 1;

                break;
            case INT:
                char str_num[MAX_STR_NUM_SIZE];
                int* num = (int*)data->values[0][i].data;
                snprintf(str_num, MAX_STR_NUM_SIZE, "%d", *num);

                memcpy(columns_value + columns_value_index, str_num, strlen(str_num));
                columns_value_index += strlen(str_num);

                memcpy(set_values + set_values_index, str_num, strlen(str_num));
                set_values_index += strlen(str_num);
                break;

        }

        if (i != count_attr - 1 ) {
            columns_name[columns_name_index] = ',';
            set_values[set_values_index] = ',';
            columns_value[columns_value_index] = ',';

            columns_name_index += 1;
            columns_value_index += 1;
            set_values_index += 1;
        }
    }

    columns_name[columns_name_size] = '\0';
    columns_value[columns_value_size] = '\0';
    set_values[set_values_size] = '\0';
    size_req += key_i->table_size + 2 * columns_name_size + columns_value_size + set_values_size;
    bd_req = wcalloc(size_req * sizeof(char));
    snprintf(bd_req, size_req, "INSERT INTO %s (%s) VALUES (%s) ON CONFLICT (%s) DO UPDATE SET %s;",
                                        key_i->table, columns_name, columns_value, key_i->column, set_values);
    return bd_req;
}
