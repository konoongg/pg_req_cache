#ifndef ALLOC_H
#define ALLOC_H

#include <stdint.h>
#include <stdlib.h>

void init_shalloc(void* buffer, size_t size);
void shfree(void* ptr);
void* shalloc(size_t size);
void* wcalloc(uint64_t size);

#endif
