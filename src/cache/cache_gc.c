#include <pthread.h>
#include <string.h>

#include "postgres.h"

#include "miscadmin.h"
#include "utils/elog.h"

#include "alloc.h"
#include "cache_gc.h"
#include "cache.h"
#include "config.h"
#include "connection.h"

extern config_cache config;
cache_gc gc;

void wake_up_cache_gc(void) {
    int err;

    err = pthread_mutex_lock(gc.not_lock);
    if (err != 0)
    {
        printf("wake_up_cache_gc: pthread_mutex_lock() failed: %s\n", strerror(err));
        abort();
    }

    event_notify(gc.gc_wthrd->not);

    err = pthread_mutex_unlock(gc.not_lock);
    if (err != 0){
        printf("wake_up_cache_gc: pthread_mutex_unlock() failed: %s\n", strerror(err));
        abort();
    }
}

static proc_status check_cache(connection* conn) {
    size_t cur_size;
    int cur_del_time = config.c_conf.ttl_s;
    do {
        cache_clean(cur_del_time);
        cur_del_time -= config.c_conf.ttl_s / 4;
        cur_size = get_cur_cache_size();
    } while (cur_size >= config.c_conf.max_storage_size && cur_del_time > 0); // if config.c_conf.ttl_s = 0 don't need do cycle, use cur_del_time > 0
    move_from_active_to_wait(conn);
    return WAIT_PROC;
}

static void* start_cache_gc(void*) {
    while (true) {
        CHECK_FOR_INTERRUPTS();
        loop_run(gc.gc_wthrd->l);
        loop_step(gc.gc_wthrd);
    }
    return NULL;
}

void init_cache_gc(void) {
    connection* timer_conn;
    connection* efd_conn;
    pthread_t cache_gc_tid;
    int efd;
    int err;
    gc.gc_wthrd = wcalloc(sizeof(wthread));

    init_wthread(gc.gc_wthrd);
    gc.gc_wthrd->l = init_loop();

    gc.not_lock = wcalloc(sizeof(pthread_mutex_t));
    err = pthread_mutex_init(gc.not_lock, NULL);
    if (err != 0){
        printf("init_cache_gc: pthread_mutex_init() failed: %s\n", strerror(err));
        abort();
    }

    efd = create_ev_notify(gc.gc_wthrd);
    efd_conn = create_connection(efd, gc.gc_wthrd);
    efd_conn->proc = check_cache;
    efd_conn->status = CHECK_CACHE;

    init_event(efd_conn, efd_conn->r_data->handle, efd_conn->fd, EVENT_READ);
    add_wait(efd_conn);
    start_event(gc.gc_wthrd->l, efd_conn->r_data->handle);

    timer_conn = create_connection(NOT_USE_FD, gc.gc_wthrd);
    timer_conn->proc = check_cache;
    timer_conn->status = CHECK_CACHE;
    init_timer(timer_conn, timer_conn->r_data->handle, config.c_conf.check_time_s, config.c_conf.check_time_s);
    add_wait(timer_conn);
    start_timer(gc.gc_wthrd->l, timer_conn->r_data->handle);

    err = pthread_create(&(cache_gc_tid), NULL, start_cache_gc, NULL);
    if (err) {
        ereport(INFO, errmsg("init_cache_gc: pthread_create error %s", strerror(err)));
        abort();
    }
}
