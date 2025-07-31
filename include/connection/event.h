#ifndef EV_H
#define EV_H

#include "ev.h"

typedef enum event_mode event_mode;
typedef struct event_loop event_loop;
typedef struct handle handle;

event_loop* init_loop(void);
void init_event(void* data, handle* h, int fd, event_mode flag);
void init_timer(void* data, handle* h, int after_time, int repeat_time);
void loop_destroy(event_loop* l);
void loop_run(event_loop* l);
void start_event(event_loop* l, handle* h);
void start_timer(event_loop* l, handle* h);
void stop_event(event_loop* l, handle* h);

struct event_loop {
    void* loop;
};

struct handle {
    void* handle;
};

enum event_mode {
    EVENT_READ,
    EVENT_WRITE,
};

#endif