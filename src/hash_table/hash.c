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


static inline uint32_t murmur_32_scramble(uint32_t k) {
    k *= 0xcc9e2d51;
    k = (k << 15) | (k >> 17);
    k *= 0x1b873593;
    return k;
}

uint64_t murmur_hash_3(void* key, int len, void* argv) {
    int* count_basket = (int*)argv;

    const uint8_t* key_ptr = (const uint8_t*)key;
    uint32_t h = 0x9747b28c;
    uint32_t k;
    for (size_t i = len >> 2; i; i--) {
        memcpy(&k, key_ptr, sizeof(uint32_t));
        key_ptr += sizeof(uint32_t);
        h ^= murmur_32_scramble(k);
        h = (h << 13) | (h >> 19);
        h = h * 5 + 0xe6546b64;
    }

    k = 0;
    for (size_t i = len & 3; i; i--) {
        k <<= 8;
        k |= key_ptr[i - 1];
    }

    h ^= murmur_32_scramble(k);
    /* Finalize. */
    h ^= len;
    h ^= h >> 16;
    h *= 0x85ebca6b;
    h ^= h >> 13;
    h *= 0xc2b2ae35;
    h ^= h >> 16;
    h %=  *count_basket;
    return h;
}

