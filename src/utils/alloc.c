#include <assert.h>
#include <errno.h>
#include <stdlib.h>

#include "postgres.h"

#include "alloc.h"
#include "logger.h"

#define FREE_BLOCK true
#define ALLOCED_BLOCK false

#define FROM_BLOCK_START true
#define FROM_BLOCK_END false

shared_allocator* allocator;

void* wcalloc(size_t size) {
    void* data = malloc(size);
    if (data == NULL) {
        cache_log(CACHE_ERROR, "init_worker: malloc error %s  - ", strerror(errno));
    }
    memset(data, 0, size);
    return data;
}

// return start mem into mark
static void* add_end_mark(char* mem_pos, int size, bool is_free) {
    end_mark* start_m = (end_mark*)mem_pos;
    end_mark* end_m = (end_mark*)(mem_pos + size);

    start_m->is_free = true;
    start_m->size = size;

    end_m->is_free = true;
    end_m->size = size;

    return mem_pos += sizeof(end_mark);
}

static int get_block_size(char* mem_pos, bool is_start) {
    end_mark* mark = (end_mark*)(mem_pos - sizeof(end_mark));

    if (!is_start) {
        mark = (end_mark*)(mem_pos + mark->size);
    }

    return mark->size;
}

static bool block_is_free(char* mem_pos, bool is_start) {
    end_mark* mark = (end_mark*)(mem_pos - sizeof(end_mark));

    if (!is_start) {
        mark = (end_mark*)(mem_pos + mark->size);
    }

    return mark->is_free;
}

static void add_free_node(free_node* node) {
    free_list* f_list = allocator->mem;
    free_node* last_start = f_list->start;
    if (last_start) {
        last_start->prev = node;
    }
    node->prev = NULL;
    node->next = last_start;
    f_list->start = node;
}

static void create_new_block(char* start_mem, int size, bool is_free) {
    char* cur_mem_pos;

    assert(size >= MIN_SIZE_BLOCK);

    cur_mem_pos = add_end_mark(start_mem, size, FREE_BLOCK);
    if (is_free) {
        add_free_node((free_node*)cur_mem_pos);
    }
}

static void rewrite_block(char* start_mem, int size, bool is_free) {
    create_new_block(start_mem - sizeof(end_mark), size, is_free);
}

void init_shared_allocator(void* mem, int size) {
    free_list* f_list = (free_list*)mem;
    int err;

    if (size < MIN_ALLOCATOR_SIZE ) {
        cache_log(CACHE_ERROR, "init_shared_allocator: get size: %d  min size %d", size, MIN_ALLOCATOR_SIZE);
    }

    memset(mem, 0, size);

    allocator = wcalloc(sizeof(shared_allocator));
    allocator->mem = mem;
    allocator->mem_size = size;
    allocator->lock = wcalloc(sizeof(pthread_mutex_t));

    err = pthread_mutex_init(allocator->lock, NULL);
    if (err != 0){
       cache_log(CACHE_ERROR, "queue_init: pthread_mutex_init() failed: %s\n", strerror(err));
    }


    create_new_block((char*)mem + sizeof(free_list), size - sizeof(free_list) - 2 * sizeof(end_mark), FREE_BLOCK);

    f_list->cur = f_list->start;
}

static void delete_free_node(free_node* node) {
    free_list* f_list = allocator->mem;

    if (node->prev) {
        node->prev->next = node->next;
    } else {
        cache_log(CACHE_INFO, "node %p", node);
        assert(node == f_list->start);
        f_list->start = node->next;
    }

    if (node->next) {
        node->next->prev = node->prev;
    }
}



static void* get_free_block(int size) {
    free_list* f_list = allocator->mem;
    free_node* start_node = f_list->cur;

    if (!start_node) {
        return NULL;
    }

    do {
        free_node* next_node = f_list->cur->next;
        if (next_node == NULL) {
            next_node = f_list->start;
            assert(next_node);
        }

        if (get_block_size((char*)f_list->cur, FROM_BLOCK_START) >= size) {
            free_node* find_node = f_list->cur;
            f_list->cur = next_node;

            cache_log(CACHE_INFO, "get_free_block");
            delete_free_node(find_node);

            return find_node;
        }

        f_list->cur = next_node;
    } while (f_list->cur != start_node);

    return NULL;
}

static void* shared_allocator_alloc(int size) {
    char* free_block;
    int size_block;
    int alloced_size;
    int err = pthread_mutex_lock(allocator->lock);
    if (err != 0) {
       cache_log(CACHE_ERROR,"shared_allocator_alloc: pthread_mutex_lock() failed: %s\n", strerror(err));
    }

    free_block = get_free_block(size);

    if (!free_block) {
        err = pthread_mutex_unlock(allocator->lock);
        if (err != 0) {
            cache_log(CACHE_ERROR,"shared_allocator_alloc: pthread_mutex_unlock() failed: %s\n", strerror(err));
        }
        return NULL;
    }

    size_block = get_block_size(free_block, FROM_BLOCK_START);

    if (size_block - size >= MIN_SIZE_BLOCK) {
        int new_block_size = size_block - size - 2 * sizeof(end_mark);
        char* new_free_block = free_block + size + sizeof(end_mark);

        create_new_block(new_free_block, new_block_size, FREE_BLOCK);
    } else {
        assert(size_block - size < 0);
        alloced_size = size_block;
    }
    rewrite_block(free_block, alloced_size, ALLOCED_BLOCK);

    err = pthread_mutex_unlock(allocator->lock);
    if (err != 0) {
       cache_log(CACHE_ERROR,"shared_allocator_alloc: pthread_mutex_unlock() failed: %s\n", strerror(err));
    }
    return free_block;
}

static neighbor_block get_neighbor (char* block) {
    neighbor_block neighbors;
    end_mark* left_end_mark = (end_mark*)(block - 2 * sizeof(end_mark));
    int block_size = get_block_size(block, FROM_BLOCK_START);
    int left_block_size = left_end_mark->size;

    neighbors.rigth = (end_mark*)(block + block_size + sizeof(end_mark));
    neighbors.left = (end_mark*)(block - left_block_size  - 3 * sizeof(end_mark));

    if ((char*)neighbors.rigth > (char*)allocator->mem + allocator->mem_size) {
        neighbors.rigth = NULL;
    }

    if ((char*)neighbors.left < (char*)allocator->mem + sizeof(free_list)) {
        neighbors.left = NULL;
    }

    return neighbors;
}

static void shared_allocator_free(void* ptr) {
    char* free_block;
    int block_size;
    int start_size;
    int end_size;
    neighbor_block neighbors;

    int err = pthread_mutex_lock(allocator->lock);
    if (err != 0) {
       cache_log(CACHE_ERROR,"shared_allocator_free: pthread_mutex_lock() failed: %s\n", strerror(err));
    }

    start_size = get_block_size(ptr, FROM_BLOCK_START);
    end_size = get_block_size(ptr, FROM_BLOCK_END);


    if (block_is_free(ptr, FROM_BLOCK_START) || block_is_free(ptr, FROM_BLOCK_END)) {
        cache_log(CACHE_ERROR, "shared_allocator_free: double free %p", ptr);
    }

    if (start_size != end_size) {
        cache_log(CACHE_ERROR, "shared_allocator_free: memmory corrupt start_size: %d end_size: %d", start_size, end_size);
    }

    neighbors = get_neighbor(ptr);
    free_block = ptr;
    block_size = get_block_size(ptr, FROM_BLOCK_START);

    if (neighbors.left && neighbors.left->is_free) {
        free_node* left_node = (free_node*)((char*)neighbors.left + sizeof(end_mark));
        cache_log(CACHE_INFO, "neighbors.left");
        delete_free_node(left_node);
        free_block = (char*)neighbors.left + sizeof(end_mark);
        block_size += (neighbors.left)->size + 2 * sizeof(end_mark);
    }

    if (neighbors.rigth && neighbors.rigth->is_free) {
        free_node* right_node = (free_node*)((char*)neighbors.rigth + sizeof(end_mark));
        cache_log(CACHE_INFO, "neighbors.rigth");
        delete_free_node(right_node);
        block_size += (neighbors.rigth)->size + 2 * sizeof(end_mark);
    }

    rewrite_block(free_block, block_size, FREE_BLOCK);

    err = pthread_mutex_unlock(allocator->lock);
    if (err != 0) {
       cache_log(CACHE_ERROR, "shared_allocator_free: pthread_mutex_unlock() failed: %s\n", strerror(err));
    }
}

void* shalloc(size_t size) {
    void* data = shared_allocator_alloc(size);
    if (data == NULL) {
        cache_log(CACHE_ERROR, "init_worker: malloc error %s  - ", strerror(errno));
    }
    memset(data, 0, size);
    return data;
}

void shfree(void* ptr) {
    shared_allocator_free(ptr);
}
