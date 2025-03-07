void* start_cache_gc() {

}

// proc_status check_cache(connection* conn) {
//     size_t cur_size = get_cur_cache_size();

//     int cur_del_time = config.c_conf.ttl_s;
//     while (cur_size >= config.c_conf.max_storage_size && cur_del_time > 0) {
//         cache_timer_delete(cur_del_time);
//         cur_del_time -= config.c_conf.ttl_s / 4;
//     }

//     move_from_active_to_wait(conn);
//     return WAIT_PROC;
// }


int init_cache_gc() {
    // timer_conn = create_connection(NOT_USE_FD, dbw.wthrd);
    // timer_conn->proc = check_cache;
    // timer_conn->status = CHECK_CACHE;
    // init_timer(timer_conn, timer_conn->r_data->handle, config.c_conf.check_time_s, config.c_conf.check_time_s);
    // add_wait(timer_conn);
    // start_timer(dbw.wthrd->l, timer_conn->r_data->handle);
}