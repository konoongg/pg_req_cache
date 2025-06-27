#include <errno.h>
#include <stdlib.h>

#include "postgres.h"
#include "utils/elog.h"

#include "alloc.h"

void* wcalloc(size_t size) {
    void* data = malloc(size);
    if (data == NULL) {
        char* err_msg = strerror(errno);
        ereport(ERROR, errmsg("init_worker: malloc error %s  - ", err_msg));
        abort();
    }
    memset(data, 0, size);
    return data;
}
