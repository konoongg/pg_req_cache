#ifndef HASH_H
#define HASH_H

#define HASH_P_31_M_100_SIZE 100

#include <stdint.h>

int hash_pow_31_mod_100(char* key);

uint64_t siphash(void* key, int len, void* argv);
uint64_t murmur_hash_3(void* key, int len, void* argv);

#endif