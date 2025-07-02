#ifndef LOGGER_H
#define LOGGER_H

typedef enum cache_log_level cache_log_level;

void cache_log(cache_log_level level, const char* fmt, ...);
void init_cache_log(cache_log_level start_level);

enum cache_log_level {
    CACHE_ERROR,
    CACHE_WARNING,
    CACHE_INFO,
    CACHE_DEBUG,
};

#endif