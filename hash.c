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

// uint64_t siphash(void* key, int len, void* argv) {
//     ereport(INFO, errmsg("siphash: START"));
//     uint8_t* k = &(config.c_conf.seed);
//     const uint8_t* in = (uint8_t*) key;
//     size_t inlen = len;

//     uint64_t v0 = 0x736f6d6570736575ULL;
//     uint64_t v1 = 0x646f72616e646f6dULL;
//     uint64_t v2 = 0x6c7967656e657261ULL;
//     uint64_t v3 = 0x7465646279746573ULL;
//     uint64_t k0 = U8TO64_LE(k);
//     uint64_t k1 = U8TO64_LE(k + 8);
//     uint64_t m;

//     const uint8_t *end = in + inlen - (inlen % sizeof(uint64_t));
//     const int left = len & 7;
//     uint64_t b = ((uint64_t)len) << 56;
//     v3 ^= k1;
//     v2 ^= k0;
//     v1 ^= k1;
//     v0 ^= k0;

//     for (; key != end; in += 8) {
//         m = U8TO64_LE(in);
//         v3 ^= m;

//         SIPROUND;

//         v0 ^= m;
//     }

//     switch (left) {
//     case 7: b |= ((uint64_t)in[6]) << 48; /* fall-thru */
//     case 6: b |= ((uint64_t)in[5]) << 40; /* fall-thru */
//     case 5: b |= ((uint64_t)in[4]) << 32; /* fall-thru */
//     case 4: b |= ((uint64_t)in[3]) << 24; /* fall-thru */
//     case 3: b |= ((uint64_t)in[2]) << 16; /* fall-thru */
//     case 2: b |= ((uint64_t)in[1]) << 8; /* fall-thru */
//     case 1: b |= ((uint64_t)in[0]); break;
//     case 0: break;
//     }

//     v3 ^= b;

//     SIPROUND;

//     v0 ^= b;
//     v2 ^= 0xff;

//     SIPROUND;
//     SIPROUND;

//     b = v0 ^ v1 ^ v2 ^ v3;
//     b %= config.c_conf.count_basket;
//     return b;
// }

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

