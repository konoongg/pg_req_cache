#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "logger.h"

cache_log_level max_level = CACHE_DEBUG;


void init_cache_log(cache_log_level start_level) {
    max_level = start_level;
}

__attribute__((format(printf, 2, 3)))
void cache_log(cache_log_level level, const char* fmt, ...) {
    if (level <= max_level) {
        char* msg = NULL;

        va_list args;
        va_start(args, fmt);

        vasprintf(&msg, fmt, args);
        va_end(args);

        if (msg) {
            char time_str[30];
            char timezone_str[10];
            char* level_str;
            long timezone_offset;
            struct timeval tv;
            struct tm *tm_info;

            switch (level) {
                case(CACHE_INFO): level_str = "INFO"; break;
                case(CACHE_ERROR): level_str = "ERROR"; break;
                case(CACHE_WARNING): level_str = "WARNING"; break;
                case(CACHE_DEBUG): level_str = "DEBUG"; break;
                default: level_str = "unknown level"; break;
            }

            gettimeofday(&tv, NULL);
            tm_info = localtime(&tv.tv_sec);
            strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
            timezone_offset = tm_info->tm_gmtoff / 3600;
            snprintf(timezone_str, sizeof(timezone_str), "%+03ld", timezone_offset);

            fprintf(stderr, "%s.%03ld %s [%d] CACHE [%d] %s:  %s\n",
                   time_str, tv.tv_usec / 1000, timezone_str,
                   getpid(), gettid(), level_str, msg);
            fflush(stderr);
            free(msg);
        }
    }

    if (level == CACHE_ERROR) {
        abort();
    }
}
