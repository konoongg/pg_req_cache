#ifndef CONFIG_PARSER_H
#define CONFIG_PARSER_H

#include <stdint.h>

#define MAX_STR_NUM_SIZE 20

typedef enum cache_mode cache_mode;
typedef struct cache_conf cache_conf;
typedef struct config_redis config_redis;
typedef struct db_conn_conf db_conn_conf;
typedef struct init_worker_conf init_worker_conf;
typedef struct proto_conf proto_conf;

void init_config(void);


struct init_worker_conf {
    int backlog_size; // listen socket backlog
    int buffer_size; // read buffer size
    int count_worker;
    int listen_port;
};

struct cache_conf {
    int count_basket_tables;
    int count_basket_values;
    int ttl_s;
    uint8_t seed;
    size_t max_storage_size; // max cache mem size (Byte)
    int check_time_s; // the time interval during which the cache is checked for records with expired ttl (seconds)
};

struct db_conn_conf {
    int count_backend; // count libpq connection with PostgreSQL
    char* dbname; // The username under which the extension communicates with PostgreSQL.
    char* user; // db use
};

struct proto_conf {
    char delim; // resp delim sym <column1>.<column2>
};

struct config_redis {
    init_worker_conf worker_conf; // io worker settings
    cache_conf c_conf; // cache settings
    db_conn_conf db_conf; // connection with postgres settings
    proto_conf p_conf; // protocol settings
};

#endif

