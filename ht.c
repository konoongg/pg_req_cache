#include <stdatomic.h>

#include "postgres.h"
#include "utils/elog.h"

#include "ht.h"

#define write_lock false
#define read_lock true

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
    for (int i = 0; i < ht->count_basket; ++i) {
        ht_basket* basket = &(ht->baskets[i]);
        ht_data* cur_data;
        ht_data* prev_data;

        basket_lock(basket, write_lock);

        cur_data = basket->first;
        prev_data = NULL;
        while (cur_data != NULL) {
            if (cur_time - cur_data->last_time >= check_time)  {
                free_data_from_ht(basket, cur_data, prev_data);
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
    ht_basket* basket;
    u_int64_t hash;

    basket = ht->baskets;
    hash = ht->hash_func(key, key_size, NULL);
    return &(ht->baskets[hash]);
}

/*
* The function checks whether the specified bucket contains data with the provided key.
* If the data exists, a reference to it is returned; otherwise, NULL is returned.
*/
ht_data* find_data_in_basket(ht_basket* basket, char* key, int key_size) {
    ht_data* data;
    data = basket->first;
    while (data != NULL) {
        if (memcmp(data->key, key, key_size) == 0 && data->key_size == key_size) {
            return data;
        }
        data = data->next;
    }
    return NULL;
}

void free_data_from_ht(ht_basket* basket, ht_data* cur_data, ht_data* prev_data) {
    if (cur_data == basket->first) {
        basket->first = cur_data->next;
    } else {
        prev_data->next = cur_data->next;
    }

    if (cur_data->next == NULL) {
        basket->last = prev_data;
    }

    atomic_fetch_sub(&(ht->cur_ht_size, cur_data->ht_data_size));

    cur_data->value_free(cur_data->v);
    free(cur_data->key);
    free(cur_data);
}

hash_table* create_ht(int count_basket, size_t max_ht_size, int ttl_s, uint64_t (*hash_func)(void* key, int len, void* argv)) {
    hash_table* ht = wcalloc(sizeof(hash_table));
    int err;

    ht->baskets = wcalloc(count_basket * siziof(ht_basket));
    ht->count_baskets = count_basket;
    ht->ttl_s = ttl_s;

    ht->hash_func = hash_func;
    atomic_store(&ht->cur_cache_size, 0);
    for (int i = 0; i < count_basket; ++i) {
        (ht->baskets[i]).lock = wcalloc(sizeof(pthread_rwlock_t));
        err = pthread_rwlock_init((ht->baskets[i]).lock, NULL);
        if (err != 0) {
            ereport(INFO, errmsg("create_ht: pthread_rwlock_init %s", strerror(err)));
            abort();
        }
    }
}

void destroy_ht(hash_table* ht) {
    int err;

    for (int i = 0; i < ht->count_baskets; ++i) {
        ht_basket* basket = &(ht->baskets[i]);
        ht_data* cur_data = basket->first;
        while (cur_data != NULL) {
            ht_data* new_data = cur_data->next;
            cur_data->value_free(cur_data->v);
            free(cur_data->key);
            free(cur_data);
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

void* get_data(hash_table* ht, char* key, int key_size) {
    ht_basket* basket;
    ht_data* data;
    void* result = NULL;

    basket = get_basket(ht, key, key_size);
    basket_lock(basket, read_lock);
    data = find_data_in_basket(basket, key, key_size);

    if (data != NULL) {
        result = data->copy(data->v);
    }

    basket_unlock(basket);
    return result;
}

void* get_or_create_data(hash_table* ht, char* key, int key_size, ht_data* (*create_ht_data)(void* data), void* data) {
    ht_basket* basket;
    ht_data* data;
    void* result = NULL;

    basket = get_basket(ht, key, key_size);
    basket_lock(basket, read_lock);
    data = find_data_in_basket(basket, key, key_size);

    if (data != NULL) {
        result = data->copy(data->v);
    } else {
        ht_data* new_data = create_ht_data(data);
        if (basket->first == NULL) {
            data = basket->first = basket->last = wcalloc(sizeof(ht_data));
        } else {
            basket->last->next = wcalloc(sizeof(ht_data));
            data = basket->last = basket->last->next;
        }
        data->next = NULL;
        data->key_size = new_data->key_size;
        data->key = new_data->key;
        data->v = new_data->v;
    }

    basket_unlock(basket);
    return result;
}


void set_data(hash_table* ht, ht_data* new_data) {
    ht_basket* basket;
    ht_data* data;
    size_t current_ht_size;

    basket = get_basket(ht, new_data->key, new_data->key_size);

    basket_lock(basket, write_lock);

    data = find_data_in_basket(basket, new_data->key, new_data->key_size);
    if (data == NULL) {
        if (basket->first == NULL) {
            data = basket->first = basket->last = wcalloc(sizeof(ht_data));
        } else {
            basket->last->next = wcalloc(sizeof(ht_data));
            data = basket->last = basket->last->next;
        }
        data->next = NULL;
        data->key_size = new_data->key_size;
        data->key = new_data->key;
        data->v = new_data->v;
    } else {
        atomic_fetch_sub(&(ht->cur_ht_size, data->ht_data_size));
        data->value_free(data->v);
        data->v = new_data->v;
    }

    atomic_fetch_add(&(ht->cur_ht_size, new_data->ht_data_size));

    // if (add_ht_size() == NEED_GC) {
    //     wake_up_cache_gc();
    // }

    data->last_time = time(NULL);
    if (data->last_time == -1) {
        char* err = strerror(errno);
        ereport(INFO, errmsg("set_cache: time error  %s", err));
        abort();
    }
    basket_unlock(basket);
}

int delete_data(hash_table* ht, char* key, int key_size) {
    ht_basket* basket;
    ht_data* data;
    ht_data* prev_data;

    basket = get_basket(ht, key, key_size);

    basket_lock(basket, write_lock);

    data = basket->first;
    prev_data = NULL;
    while (data != NULL) {
        if (memcmp(data->key, key, key_size) == 0 && data->key_size == key_size) {
            break;
        }
        prev_data = data;
        data = data->next;
    }

    if (data == NULL) {
        basket_unlock(basket);
        return 0;
    }

    free_data_from_ht(basket, data, prev_data);
    basket_unlock(basket);

    return 1;
}