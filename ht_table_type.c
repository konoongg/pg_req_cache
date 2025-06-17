#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "alloc.h"
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
    table_data* new_data = shalloc(sizeof(table_data));
    new_data->uniq_num = data->uniq_num;
    return new_data;
}

void free_data_table(ht_data* data) {
    find_table_key* find_key = data->find_key;
    shfree(find_key->key);
    shfree(data->find_key);
    shfree(data);
}

void value_free_table(void* value) {
    create_ht_data* data = (create_ht_data*)value;
    find_table_key* f_table =(find_table_key*)data->find_key;
    shfree(f_table->key);
    shfree(f_table);
    shfree(data->value);
}
