#include <pthread.h>
#include <stdatomic.h>

#include "postgres.h"
#include "utils/elog.h"

#include "alloc.h"
#include "ht.h"

#define write_lock false
#define read_lock true

ht_basket* get_basket(hash_table* ht, char* key, int key_size);
ht_data* find_data_in_basket(hash_table* ht, ht_basket* basket, void* find_key);
void basket_lock(ht_basket* basket, bool is_read_lock);
void basket_unlock(ht_basket* basket);
void free_data_from_ht(hash_table* ht, ht_basket* basket, ht_data* cur_data, ht_data* prev_data);

void basket_lock(ht_basket* basket, bool is_read_lock) {
    if (is_read_lock) {
        int err = pthread_rwlock_rdlock(basket->lock);
        if (err != 0) {
            ereport(INFO, errmsg("basket_lock: pthread_rwlock_rdlock() failed: %s\n", strerror(err)));
            abort();
        }
    } else {
        int err = pthread_rwlock_wrlock(basket->lock);
        if (err != 0) {
            ereport(INFO, errmsg("basket_lock: pthread_rwlock_rdlock() failed: %s\n", strerror(err)));
            abort();
        }
    }
}

void basket_unlock(ht_basket* basket) {
    int err = pthread_rwlock_unlock(basket->lock);
    if (err != 0) {
        ereport(INFO, errmsg("basket_lock: pthread_rwlock_unlock() failed: %s\n", strerror(err)));
        abort();
    }
}

void ht_timer_delete(hash_table* ht, time_t check_time) {
    time_t cur_time = time(NULL);
    for (int i = 0; i < ht->count_baskets; ++i) {
        ht_basket* basket = &(ht->baskets[i]);
        ht_data* cur_data;
        ht_data* prev_data;

        basket_lock(basket, write_lock);

        cur_data = basket->first;
        prev_data = NULL;
        while (cur_data != NULL) {
            if (cur_time - cur_data->last_time >= check_time)  {
                free_data_from_ht(ht, basket, cur_data, prev_data);
            } else {
                prev_data = cur_data;
            }
            cur_data = cur_data->next;
        }
        basket_unlock(basket);
    }
}

//A function to retrieve the corresponding ht bucket based on a string.
ht_basket* get_basket(hash_table* ht, char* key, int key_size) {
    u_int64_t hash;
    hash = ht->hash_func(key, key_size, NULL);
    return &(ht->baskets[hash]);
}

/*
* The function checks whether the specified bucket contains data with the provided key.
* If the data exists, a reference to it is returned; otherwise, NULL is returned.
*/
ht_data* find_data_in_basket(hash_table* ht, ht_basket* basket, void* find_key) {
    ht_data* data;
    data = basket->first;
    while (data != NULL) {
        if (ht->cmp_key(data->find_key, find_key)) {
            return data;
        }
        data = data->next;
    }
    return NULL;
}

void free_data_from_ht(hash_table* ht, ht_basket* basket, ht_data* cur_data, ht_data* prev_data) {
    if (cur_data == basket->first) {
        basket->first = cur_data->next;
    } else {
        prev_data->next = cur_data->next;
    }

    if (cur_data->next == NULL) {
        basket->last = prev_data;
    }

    atomic_fetch_sub(&(ht->cur_ht_size), cur_data->ht_data_size);
    ht->free_data(ht->value_free, cur_data);
}

hash_table* create_ht(create_ht_info* info) {
    hash_table* ht = wcalloc(sizeof(hash_table));

    ht->count_baskets = info->count_basket;
    ht->baskets = wcalloc(ht->count_baskets * sizeof(ht_basket));
    ht->ttl_s = info->ttl_s;
    ht->max_ht_size = info->max_ht_size;

    ht->hash_func = info->hash_func;
    ht->cmp_key = info->cmp_key;
    ht->copy = info->copy;
    ht->free_data = info->free_data;
    ht->value_free = info->value_free;

    atomic_store(&(ht->cur_ht_size), 0);

    for (int i = 0; i < ht->count_baskets ; ++i) {
        int err;

        (ht->baskets[i]).lock = wcalloc(sizeof(pthread_rwlock_t));
        err = pthread_rwlock_init((ht->baskets[i]).lock, NULL);
        if (err != 0) {
            ereport(INFO, errmsg("create_ht: pthread_rwlock_init %s", strerror(err)));
            abort();
        }
    }
    return ht;
}

void destroy_ht(hash_table* ht) {
    for (int i = 0; i < ht->count_baskets; ++i) {
        int err;
        ht_basket* basket = &(ht->baskets[i]);
        ht_data* cur_data = basket->first;
        while (cur_data != NULL) {
            ht_data* new_data = cur_data->next;
            ht->value_free(cur_data->value);
            ht->free_data(ht->value_free, cur_data);
            cur_data = new_data;
        }

        err = pthread_rwlock_destroy(basket->lock);
        if (err != 0) {
            ereport(INFO, errmsg("free_cache: pthread_rwlock_destroy %s", strerror(err)));
            abort();
        }
        free((void*) basket->lock);
    }
    free(ht->baskets);
    free(ht);
}

void* get_data(hash_table* ht, find_ht_data* find) {
    ht_basket* basket;
    ht_data* data;
    void* result = NULL;

    basket = get_basket(ht, find->hash_key, find->hash_key_size);
    basket_lock(basket, read_lock);
    data = find_data_in_basket(ht, basket, find->find_key);

    if (data != NULL) {
        result = ht->copy(data->value);
    }

    basket_unlock(basket);
    return result;
}


void set_data(hash_table* ht, create_ht_data* new_data) {
    ht_basket* basket;
    ht_data* data;
    int data_size;

    basket = get_basket(ht, new_data->hash_key, new_data->hash_key_size);

    basket_lock(basket, write_lock);

    data = find_data_in_basket(ht, basket, new_data->find_key);
    if (data == NULL) {
        if (basket->first == NULL) {
            data = basket->first = basket->last = wcalloc(sizeof(ht_data));
        } else {
            basket->last->next = wcalloc(sizeof(ht_data));
            data = basket->last = basket->last->next;
        }
        data->next = NULL;
        data->find_key = new_data->find_key;
        data->value = new_data->value;
    } else {
        atomic_fetch_sub(&(ht->cur_ht_size), data->ht_data_size);
        ht->value_free(data->value);
        data->value = new_data->value;
    }

    data_size = new_data->find_key_size + new_data->value_size + sizeof(ht_data);

    atomic_fetch_add(&(ht->cur_ht_size), data_size);

    data->last_time = time(NULL);
    if (data->last_time == -1) {
        char* err = strerror(errno);
        ereport(INFO, errmsg("set_cache: time error  %s", err));
        abort();
    }
    basket_unlock(basket);
}

void set_data_if_not_exist(hash_table* ht, create_ht_data* new_data) {
    ht_basket* basket;
    ht_data* data;

    basket = get_basket(ht, new_data->hash_key, new_data->hash_key_size);

    basket_lock(basket, write_lock);

    data = find_data_in_basket(ht, basket, new_data->find_key);
    if (data == NULL) {
        if (basket->first == NULL) {
            data = basket->first = basket->last = wcalloc(sizeof(ht_data));
        } else {
            basket->last->next = wcalloc(sizeof(ht_data));
            data = basket->last = basket->last->next;
        }
        data->next = NULL;
        data->find_key = new_data->find_key;
        data->value = new_data->value;

        atomic_fetch_add(&(ht->cur_ht_size), new_data->find_key_size + new_data->value_size + sizeof(ht_data));
        data->last_time = time(NULL);
        if (data->last_time == -1) {
            char* err = strerror(errno);
            ereport(INFO, errmsg("set_cache: time error  %s", err));
            abort();
        }
    } else {
        ht->value_free(new_data);
    }
    basket_unlock(basket);
}

int delete_data(hash_table* ht, find_ht_data* find) {
    ht_basket* basket;
    ht_data* data;
    ht_data* prev_data;

    basket = get_basket(ht, find->hash_key, find->hash_key_size);

    basket_lock(basket, write_lock);

    data = basket->first;
    prev_data = NULL;
    while (data != NULL) {
        if (ht->cmp_key(data->find_key, find->find_key)) {
            break;
        }
        prev_data = data;
        data = data->next;
    }

    if (data == NULL) {
        basket_unlock(basket);
        return 0;
    }

    free_data_from_ht(ht, basket, data, prev_data);
    basket_unlock(basket);

    return 1;
}