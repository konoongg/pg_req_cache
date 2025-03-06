#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "postgres.h"
#include "utils/elog.h"

#include "alloc.h"
#include "config.h"

void defalt_setting_init(void);

extern config_redis config;

void defalt_setting_init(void) {
    // FILE* fp;
    // int res;

    config.c_conf.count_basket = 100000; // cache basket
    config.c_conf.ttl_s = 5; // cache ttl

    config.c_conf.seed = 101; // hash seed

    // fp = fopen("/dev/urandom","r");
    // if (fp == NULL) {
    //     char* err = strerror(errno);
    //     ereport(INFO, errmsg("defalt_setting_init: fopen error %s", err));
    //     abort();
    // }
    // res = fread(&(config.c_conf.seed), sizeof(uint8_t),1 , fp);

    // if (res != 1) {
    //     ereport(INFO, errmsg("defalt_setting_init: fread error"));
    //     abort();
    // }

    // if (fclose(fp) == 0) {
    //     char* err = strerror(errno);
    //     ereport(INFO, errmsg("defalt_setting_init: fclose error %s", err));
    //     abort();
    // }

    config.worker_conf.backlog_size = 512; // listen socket backlog
    config.worker_conf.buffer_size = 512; // read buffer size
    config.worker_conf.count_worker = 4;
    config.worker_conf.listen_port = 6379;

    config.db_conf.count_backend = 1; // count libpq backend
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
