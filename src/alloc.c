#include <errno.h>
#include <stdlib.h>

#include "postgres.h"

#include "alloc.h"
#include "logger.h"

void* wcalloc(size_t size) {
    void* data = malloc(size);
    if (data == NULL) {
        cache_log(CACHE_ERROR, "init_worker: malloc error %s  - ", strerror(errno));
    }
    memset(data, 0, size);
    return data;
}
