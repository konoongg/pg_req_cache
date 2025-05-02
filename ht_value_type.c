#include "ht_value_type.h"
#include "ht.h"

bool cmp_table_key(void* find_key_1, void* find_key_2) {
    find_value_key* key_1 = find_key_1;
    find_value_key* key_2 = find_key_2;

    if (memcmp(key_1->key, key_2->key, key_1->key_size) == 0 &&
            key_1->key_size == key_2->key_size &&
            key_1->table_num == key_2->table_num) {
        return true;
    }

    return false;
}

void* copy_value(void* data) {
    value* v = (value*)data;

    int count_tuples = v->count_tuples;
    int count_fields = v->count_fields;

    value* new_v = wcalloc(sizeof(value));
    new_v->count_tuples = count_tuples;
    new_v->count_fields = count_fields;
    new_v->values = wcalloc(count_tuples * sizeof(attr*));

    for (int i = 0; i < count_tuples; ++i) {
        new_v->values[i] = wcalloc(count_fields * sizeof(attr));
        for (int j = 0; j < count_fields; ++j ) {
            int column_name_size = strlen(v->values[i][j].column_name) + 1;
            attr* a = &(new_v->values[i][j]);
            a->type = v->values[i][j].type;

            a->column_name = wcalloc(column_name_size * sizeof(char));
            memcpy(a->column_name, v->values[i][j].column_name, column_name_size);
            a->data = wcalloc(sizeof(db_data));

            switch (a->type) {
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