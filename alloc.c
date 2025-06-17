#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>

#include "rpmalloc/rpmalloc.h"

#include "postgres.h"
#include "utils/elog.h"

#include "alloc.h"

static rpmalloc_heap_t* custom_heap = NULL;
static void* custom_buffer = NULL;
static size_t custom_buffer_size = 0;
static bool buffer_allocated = false;


rpmalloc_interface_t memory_interface;

void* custom_memory_map(size_t size, size_t alignment, size_t* offset, size_t* mapped_size) {
    if (!buffer_allocated && custom_buffer ) {
        buffer_allocated = true;
        *offset = 0;
        *mapped_size = custom_buffer_size;
        return custom_buffer;
    }
    return NULL;
}


void noop_memory_unmap(void* address, size_t offset, size_t size) {
    return;
}

void noop_memory_commit(void* address, size_t size) {
    return;
}

void noop_memory_decommit(void* address, size_t size) {
    return;
}

int noop_map_fail_callback(size_t size) {
    return 0;
}

void noop_error_callback(const char* message) {
    return;
}

void init_shalloc(void* buffer, size_t size) {
    ereport(INFO, errmsg("init_shalloc: start"));
    custom_buffer_size = size;
    buffer_allocated = false;

    memory_interface.memory_map = custom_memory_map;
    memory_interface.memory_unmap = noop_memory_unmap;
    memory_interface.memory_commit = noop_memory_commit;
    memory_interface.memory_decommit = noop_memory_decommit;
    memory_interface.map_fail_callback = noop_map_fail_callback;
    memory_interface.error_callback = noop_error_callback;

    ereport(INFO, errmsg("init_shalloc: test %p ", (&memory_interface)->memory_commit));
    if (rpmalloc_initialize_config(&memory_interface, NULL) != 0) {
        ereport(ERROR, errmsg("init_shalloc: can't init allocator"));
        return;
    }

    ereport(INFO, errmsg("init_shalloc: test2 "));
    custom_heap = rpmalloc_heap_acquire();
    if (!custom_heap) {
        ereport(ERROR, errmsg("init_shalloc: can't create custom heap"));
        return;
    }

    ereport(INFO, errmsg("init_shalloc: finish"));
}

void* shalloc(size_t size) {
    void* ptr = rpmalloc_heap_alloc(custom_heap, size);
    if (ptr == NULL) {
        char* err_msg = strerror(errno);
        ereport(ERROR, errmsg("init_worker: malloc error %s  - ", err_msg));
        abort();
    }
    memset(ptr, 0, size);
    return ptr;
}

void shfree(void* ptr) {
    if (ptr) {
        rpmalloc_heap_free(custom_heap, ptr);
    }
}

void* wcalloc(size_t size) {
    void* ptr = malloc(size);
    if (ptr == NULL) {
        char* err_msg = strerror(errno);
        ereport(ERROR, errmsg("init_worker: malloc error %s  - ", err_msg));
        abort();
    }
    memset(ptr, 0, size);
    return ptr;
}
