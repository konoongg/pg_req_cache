#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>


#include "postgres.h"
#include "utils/elog.h"

#include "socket_wrapper.h"

int socket_set_nonblock(int socket_fd);

// do socket non block or return err
int socket_set_nonblock(int socket_fd) {
    int flags = fcntl(socket_fd, F_GETFL, 0);
    if (flags < 0) {
        return -1;
    }
    if (fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        return -1;
    }
    return 0;
}

//create listen tcp socket and return err ro fd thos socket
int init_listen_socket(int listen_port, int backlog_size) {
    int err;
    const int val = 1;
    int listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in sockaddr;

    if (listen_socket == -1) {
        char* err = strerror(errno);
        ereport(INFO, errmsg("init_listen_socket: err socket - %s", err));
        return -1;
    }

    if (socket_set_nonblock(listen_socket) == -1) {
        ereport(INFO, errmsg("init_listen_socket: for listen socket set nonblocking mode fail"));
        return -1;
    }

    memset(&sockaddr, 0, sizeof(sockaddr));
    sockaddr.sin_family = AF_INET;
    sockaddr.sin_port = htons(listen_port);
    sockaddr.sin_addr.s_addr = INADDR_ANY;

    err = setsockopt(listen_socket, SOL_SOCKET, SO_REUSEPORT, &val, sizeof(val));
	if (err == -1) {
        char* err_msg  =  strerror(errno);
        ereport(INFO, errmsg("setsockopt: %s", err_msg));
        return -1;
    }

    err = bind(listen_socket, (struct sockaddr *)&sockaddr, sizeof(sockaddr));
    if (err == -1) {
        char* err_msg =  strerror(errno);
        ereport(INFO, errmsg("init_listen_socket: bind error -  %s %d", err_msg, listen_socket));
        return -1;
    }

    err = listen(listen_socket, backlog_size);
    if (err == -1) {
        char* err_msg  =  strerror(errno);
        ereport(INFO, errmsg("init_listen_socket: listen error - %s", err_msg));
        return -1;
    }

    //ereport(INFO, errmsg("init listen socket"));
    return listen_socket;
}

//configure socket, use saved listen_socket
int init_socket (int socket_fd) {
    if (socket_set_nonblock(socket_fd) == -1) {
        ereport(WARNING, errmsg("for listen socket set nonblocking mode fail"));
        return -1;
    }
    return 0;
}