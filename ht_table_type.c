#include "ht_table_type.h"
#include "ht.h"

bool cmp_table_key(void* find_key_1, void* find_key_2) {
    find_table_key* key_1 = find_key_1;
    find_table_key* key_2 = find_key_2;

    if (memcmp(key_1->key, key_2->key, key_1->key_size) == 0 && key_1->key_size == key_2->key_size) {
        return true;
    }

    return false;
}

void* copy_table(void* value) {
    table_data* data = (table_data*)value;
    table_data* new_data = wcalloc(sizeof(table_data));
    new_data->name_size = data->name_size;
    new_data->name = wcalloc(new_data->name_size * sizeof(char));
    memcpy(new_data->name,  data->name, new_data->name_size);
    new_data->uniq_num = data->uniq_num;

    return new_data;
}

void free_data_table(void (*value_free)(void* value), ht_data* data) {
    find_table_key* find_key = data->find_key;
    free(find_key->key);
    value_free(data->value);
    free(data->find_key);
    free(data);
}

void value_free_table(void* value) {
    table_data* data = (table_data*)value;
    free(data->name);
    free(data;)
}
