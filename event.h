#ifndef EVENT_H_
#define EVENT_H_

#include <sys/timerfd.h>
#include <stdint.h>

struct event_handler {
    int fd;
    void *data;
    int (*handler)(int fd, void *data);
};

int run_event_loop(int ev_loop);

int create_event_loop();
int create_event(int ev_loop, uint64_t value, struct event_handler *ev);
int create_timer(int ev_loop, const struct itimerspec *timeout, struct event_handler *ev);

#endif /* EVENT_H_ */
