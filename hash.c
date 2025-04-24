#include <stdint.h>
#include <string.h>

#include "postgres.h"
#include "utils/elog.h"

#include "config.h"
#include "hash.h"

#define U8TO64_LE(p) (*((uint64_t*)(p)))
#define ROTL(x, b) (uint64_t)(((x) << (b)) | ((x) >> (64 - (b))))

#define SIPROUND                                                               \
    do {                                                                       \
        v0 += v1;                                                              \
        v1 = ROTL(v1, 13);                                                     \
        v1 ^= v0;                                                              \
        v0 = ROTL(v0, 32);                                                     \
        v2 += v3;                                                              \
        v3 = ROTL(v3, 16);                                                     \
        v3 ^= v2;                                                              \
        v0 += v3;                                                              \
        v3 = ROTL(v3, 21);                                                     \
        v3 ^= v0;                                                              \
        v2 += v1;                                                              \
        v1 = ROTL(v1, 17);                                                     \
        v1 ^= v2;                                                              \
        v2 = ROTL(v2, 32);                                                     \
    } while (0)

extern config_redis config;

int hash_pow_31_mod_100(char* key) {
    char cur_sym;
    int cur_index;
    int key_size = strlen(key);
    uint64_t  hash = 0;
    uint64_t k = 31;

    for (int i = 0; i < key_size - 2; ++i) {
        k *= 31;
    }

    cur_index = 0;
    cur_sym = key[cur_index];

    while (cur_sym != '\0') {
        hash += k * cur_sym;
        k /= 31;
        cur_index++;
        cur_sym = key[cur_index];
    }
    return hash % 100;
}


uint64_t murmur_hash_2(void* key, int len, void* argv) {
    const unsigned int m = 0x5bd1e995;
    const unsigned int seed = 0;
    const int r = 24;
    unsigned int k = 0;

    unsigned int h = seed ^ len;
    const unsigned char * data = (const unsigned char *)key;

    while (len >= 4) {
        k = data[0];
        k |= data[1];
        k |= data[2];
        k |= data[3];

        k *= m;
        k ^= k >> r;
        k *= m;

        h *= m;
        h ^=k;
        data +=4;
        len -= 4;
    }

    if (len == 3) {
        h ^= data[2] << 16;
    }
    if (len >= 2) {
        h ^= data[1] << 8;
    }
    if (len >= 1) {
        h ^= data[0];
    }

    h ^= h >> 13;
    h *= m;
    h ^= h >> 15;
    h %=  config.c_conf.count_basket;
    return h;
}

