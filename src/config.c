#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "postgres.h"

#include "utils/elog.h"

#include "alloc.h"
#include "config.h"


extern config_cache config;

static void defalt_setting_init(void) {

    config.c_conf.count_basket_values = 100003; // cache basket
    config.c_conf.count_basket_tables = 101; // cache basket
    config.c_conf.ttl_s = 0; // cache ttl
    config.c_conf.max_storage_size = (size_t)1024 * 1024 * 1024 * 1024;
    config.c_conf.seed = 101; // hash seed

    config.c_conf.invalid_update = false;

    config.worker_conf.backlog_size = 512; // listen socket backlog
    config.worker_conf.buffer_size = 512; // read buffer size
    config.worker_conf.count_worker = 4;
    config.worker_conf.listen_port = 6379;

    config.db_conf.count_backend = 4; // count libpq backend
    config.db_conf.dbname = wcalloc(9 * sizeof(char));
    config.db_conf.user = wcalloc(9 * sizeof(char));
    memcpy(config.db_conf.dbname, "postgres", 9);
    memcpy(config.db_conf.user, "postgres", 9);

    config.p_conf.delim = '.';
}

// Initialize the config value from the corresponding file.
// If the file does not exist, set the default value for all config parameters.
void init_config(void) {
    defalt_setting_init();
}
