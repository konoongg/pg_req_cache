#ifndef ALLOC_H
#define ALLOC_H

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#define MIN_ALLOCATOR_SIZE (sizeof(free_list) + 2 * sizeof(end_mark) +  sizeof(free_list))
#define MIN_SIZE_BLOCK sizeof(free_node) + 2 * sizeof(end_mark)

typedef struct free_node free_node;
typedef struct free_list free_list;
typedef struct neighbor_block neighbor_block;
typedef struct end_mark end_mark;

void shfree(void* ptr);
void* shalloc(size_t size);
void* wcalloc(uint64_t size);
void init_shared_allocator(void* mem, int size);

struct neighbor_block {
    end_mark* left;
    end_mark* rigth;
};

#pragma pack(push, 1)
struct end_mark {
    int size;
    bool is_free;
};
#pragma pack(pop)


struct free_node {
    free_node* next;
    free_node* prev;
};

struct free_list {
    free_node* start;
    free_node* cur;
};

#endif
