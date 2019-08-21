#include "event.h"

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>

#include <assert.h>
#include <syslog.h>
#include <unistd.h>

#include "error_handling.h"


#define EPOLL_MAX_EVENTS 32


int register_event(int ev_loop, int events, struct event_handler *ev) {
    assert(ev_loop != -1);
    assert(ev != NULL);
    assert(ev->fd != -1);
    assert(ev->handler != NULL);

    struct epoll_event event;
    event.events = events;
    event.data.ptr = ev;
    int result = 0;
    HANDLE_POSIX_RESULT(result = epoll_ctl(ev_loop, EPOLL_CTL_ADD, ev->fd, &event),
                        (void)result,
                        "fd=%d: epoll_ctl: register_event", ev->fd);
    return result;
}

int create_event_loop() {
    int ev_loop = epoll_create1(EPOLL_CLOEXEC);
    HANDLE_POSIX_RESULT(ev_loop, (void)ev_loop, "epoll_create1: create_event_loop");
    return ev_loop;
}

int create_event(int ev_loop, uint64_t value, struct event_handler *ev) {
    assert(ev_loop != -1);
    assert(ev != NULL);

    ev->fd = eventfd(value, EFD_CLOEXEC | EFD_NONBLOCK);
    HANDLE_POSIX_RESULT(ev->fd, return -1, "eventfd: create_event");
    syslog(LOG_DEBUG, "fd=%d: event created", ev->fd);
    HANDLE_RESULT(register_event(ev_loop, EPOLLIN | EPOLLERR, ev) == -1,
                  goto FAIL, "fd=%d: register_event: create_event", ev->fd);
    return 0;

FAIL:
    HANDLE_POSIX_RESULT(close(ev->fd), (void)ev, "fd=%d: close: create_event", ev->fd);
    syslog(LOG_DEBUG, "fd=%d: event destroyed", ev->fd);
    ev->fd = -1;
    return -1;
}

int create_timer(int ev_loop, const struct itimerspec* timeout, struct event_handler *ev) {
    assert(ev_loop != -1);
    assert(ev != NULL);

    ev->fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    HANDLE_POSIX_RESULT(ev->fd, return -1, "timerfd_create: create_timer");
    syslog(LOG_DEBUG, "fd=%d: timer created", ev->fd);
    HANDLE_RESULT(register_event(ev_loop, EPOLLIN | EPOLLERR, ev) == -1,
                  goto FAIL, "fd=%d: register_event: create_timer", ev->fd);
    HANDLE_POSIX_RESULT(timerfd_settime(ev->fd, 0, timeout, NULL),
                        goto FAIL, "fd=%d: timerfd_settime: create_timer", ev->fd);
    return 0;

FAIL:
    HANDLE_POSIX_RESULT(close(ev->fd), (void)ev, "fd=%d: close: create_timer", ev->fd);
    syslog(LOG_DEBUG, "fd=%d: timer destroyed", ev->fd);
    ev->fd = -1;
    return -1;
}


int handle_event(struct epoll_event *events, int eventslen) {
    assert(events != NULL);
    int result = 0;
    for(int i = 0; i < eventslen; ++i) {
        struct event_handler *ev = (struct event_handler*)events[i].data.ptr;
        //        syslog(LOG_DEBUG, "fd=%d: handling event", ev->fd);
        int r = (*ev->handler)(ev->fd, ev->data);
        syslog(LOG_DEBUG, "fd=%d: event handled, result=%d", ev->fd, r);
        if(r == -1) result = r;
    }
    return result;
}

int run_event_loop(int ev_loop) {
    struct epoll_event events[EPOLL_MAX_EVENTS];
    syslog(LOG_DEBUG, "entering event loop");
    for(;;) {
        int nfds = epoll_wait(ev_loop, events, EPOLL_MAX_EVENTS, -1);
        HANDLE_POSIX_RESULT(nfds, return -1, "epoll_wait");
        if(handle_event(events, nfds) == -1) {
            syslog(LOG_DEBUG, "leaving event loop");
            return -1;
        }
    }
    return 0;
}
