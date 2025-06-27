#include <stdlib.h>
#include <string.h>

#include "postgres.h"

#include "utils/elog.h"

#include "alloc.h"
#include "ht_response_type.h"
#include "ht.h"
#include "storage_data.h"

bool cmp_response_key(void* find_key_1, void* find_key_2) {
    find_value_key* key_1 = find_key_1;
    find_value_key* key_2 = find_key_2;
    if (memcmp(key_1->key, key_2->key, key_1->key_size) == 0 &&
            key_1->key_size == key_2->key_size &&
            key_1->table_num == key_2->table_num) {
        return true;
    }
    return false;
}

void* copy_response(void* data) {
    cache_response* v = (cache_response*)data;

    int count_tuples = v->count_tuples;
    int count_fields = v->count_fields;

    cache_response* new_v = wcalloc(sizeof(cache_response));
    new_v->count_tuples = count_tuples;
    new_v->count_fields = count_fields;
    new_v->values = wcalloc(count_tuples * sizeof(cache_attr*));
    new_v->columns = wcalloc(count_fields * sizeof(column*));

    for (int i = 0; i < count_fields; ++i) {
        new_v->columns[i] = v->columns[i];
    }

    for (int i = 0; i < count_tuples; ++i) {
        new_v->values[i] = wcalloc(count_fields * sizeof(cache_attr));
        for (int j = 0; j < count_fields; ++j ) {
            cache_attr* a = &(new_v->values[i][j]);
            a->data = wcalloc(sizeof(db_data));

            switch (new_v->columns[j]->type) {
                case INT:
                    a->data->num = v->values[i][j].data->num;
                    break;
                case STRING:
                    int str_size = v->values[i][j].data->str.size;
                    a->data->str.str = wcalloc(str_size * sizeof(char));
                    memcpy(a->data->str.str, v->values[i][j].data->str.str, str_size);
                    a->data->str.size = str_size;
                    break;
            }
        }
    }
    return new_v;
}

void free_data_response( ht_data* data) {
    find_value_key* find_key = data->find_key;
    free(find_key->key);
    free(data->find_key);
    free(data);
}

void value_free_response(void* data) {
    cache_response* v = (cache_response*)data;
    int count_tuples = v->count_tuples;
    int count_field = v->count_fields;

     for (int i = 0; i < count_tuples; ++i) {
        for (int j = 0; j < count_field; ++j) {
            free(v->values[i][j].data);
        }
        free(v->values[i]);
    }
    free(v->columns);
    free(v->values);
    free(v);
}