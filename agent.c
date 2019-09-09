#include "agent.h"

#include <sys/types.h>
#include <sys/socket.h>

#include <assert.h>
#include <inttypes.h>
#include <ifaddrs.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "event.h"
#include "error_handling.h"
#include "influxdb.h"

#define MAX_MESSAGE_SIZE 65535

int create_sink(const char *remote, const char *service) {
    assert(remote != NULL);
    assert(service != NULL);

    int s = -1;
    struct addrinfo hint;
    struct addrinfo *result = NULL;

    memset(&hint, 0, sizeof(hint));
    hint.ai_family = AF_UNSPEC;
    hint.ai_socktype = SOCK_DGRAM;
    hint.ai_flags = AI_NUMERICSERV;

    int ec = getaddrinfo(remote, service, &hint, &result);
    HANDLE_RESULT(ec != 0, return -1, "getaddrinfo: %s", gai_strerror(ec));

    for(struct addrinfo *ai = result; ai != NULL; ai = ai->ai_next) {
        HANDLE_POSIX_RESULT(s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol),
                            goto NEXT_ADDRESS, "socket");
        HANDLE_POSIX_RESULT(connect(s, ai->ai_addr, ai->ai_addrlen),
                            goto NEXT_ADDRESS, "fd=%d: connect", s);
        syslog(LOG_DEBUG, "fd=%d: sink created", s);
        break;
NEXT_ADDRESS:
        HANDLE_POSIX_RESULT(close(s), (void)s, "fd=%d: close", s);
    }
    freeaddrinfo(result);
    return s;
}

int read_file(const char *filename,
              char *content,
              size_t *contentlen) {
    assert(filename != NULL);
    assert(content != NULL);
    assert(contentlen != NULL);

    int fd = -1;
    HANDLE_POSIX_RESULT(fd = open(filename, O_CLOEXEC | O_NONBLOCK, O_RDONLY),
                        return -1, "read_file(%s): open", filename);

    int r = read(fd, content, *contentlen);
    HANDLE_POSIX_RESULT(r, goto CLEANUP, "read_file(%s): read", filename);
    HANDLE_RESULT((size_t) r >= *contentlen,
                  goto CLEANUP, "read_file(%s): buffer too small", filename);
    content[*contentlen - 1] = 0;
    *contentlen = r;

CLEANUP:
    HANDLE_POSIX_RESULT(close(fd), (void)fd, "read_file(%s): close", filename);
    return *contentlen;
}

int serialize_softnet_stat(const char *hostname,
                           const struct timespec *ts,
                           char *message, size_t *messagelen) {
    assert(hostname != NULL);
    assert(ts != NULL);
    assert(message != NULL);
    assert(messagelen != NULL);

    char stat[65535];
    size_t statlen = sizeof(stat);
    HANDLE_RESULT(read_file("/proc/net/softnet_stat", stat, &statlen) < 0,
                  return -1, "serialize_softnet_stat: read_file");
    stat[statlen] = 0;
    HANDLE_RESULT(influxdb_serialize_softnet_stat(stat, hostname, ts, message, messagelen) < 0,
                  return -1, "serialize_softnet_stat: influxdb_serialize_softnet_stat");
    return 0;
}

int serialize_net_stat(const char *hostname,
                       const struct timespec *ts,
                       char *message, size_t *messagelen) {
    assert(hostname != NULL);
    assert(ts != NULL);
    assert(message != NULL);
    assert(messagelen != NULL);

    static const char *tags[] = {
        "Ip", "Icmp", "IcmpMsg", "Tcp", "Udp", /* SNMP tags */
        "TcpExt", "IpExt", /* NetStat tags */
        NULL
    };

    char stat[65535];
    size_t snmplen = sizeof(stat);
    size_t netstatlen = sizeof(stat);

    HANDLE_RESULT(read_file("/proc/net/snmp", stat, &snmplen) < 0,
                  return -1, "serialize_net_stat[/proc/net/snmp]: read_file");

    netstatlen -= snmplen;
    HANDLE_RESULT(read_file("/proc/net/netstat", stat + snmplen, &netstatlen) < 0,
                  return -1, "serialize_net_stat[/proc/net/netstat]: read_file");
    stat[snmplen + netstatlen] = 0;
    HANDLE_RESULT(influxdb_serialize_net_stat(stat, tags, hostname, ts, message, messagelen) < 0,
                  return -1, "serialize_net_stat: influxdb_serialize_net_stat");
    return 0;
}

int serialize_proc_stat(const char *hostname,
                        const struct timespec *ts,
                        char *message, size_t *messagelen) {
    assert(hostname != NULL);
    assert(ts != NULL);
    assert(message != NULL);
    assert(messagelen != NULL);

    char proc[65535];
    size_t proclen = sizeof(proc);
    HANDLE_RESULT(read_file("/proc/stat", proc, &proclen) < 0,
                  return -1, "serialize_proc_stat: read_file");
    proc[proclen] = 0;
    HANDLE_RESULT(influxdb_serialize_proc_stat(proc, hostname, ts, message, messagelen) < 0,
                  return -1, "serialize_proc_stat: influxdb_serialize_proc_stat");
    return 0;
}

int serialize_nic_stat(const char *hostname,
                       const struct timespec *ts,
                       char *message, size_t *messagelen) {
    assert(hostname != NULL);
    assert(ts != NULL);
    assert(message != NULL);
    assert(messagelen != NULL);

    return influxdb_serialize_nic_stat(hostname, ts, message, messagelen);
}

int serialize_memory_stat(const char *hostname,
                          const struct timespec *ts,
                          char *message, size_t *messagelen) {
    assert(hostname != NULL);
    assert(ts != NULL);
    assert(message != NULL);
    assert(messagelen != NULL);

    return influxdb_serialize_memory_stat(hostname, ts, message, messagelen);
}


typedef int(*serializer)(const char *hostname,
                         const struct timespec *ts,
                         char *message, size_t *messagelen);

static const serializer serializers[] = {
    &serialize_proc_stat,
    &serialize_net_stat,
    &serialize_softnet_stat,
    &serialize_nic_stat,
    &serialize_memory_stat,
    NULL
};


struct agent_context {
    int sink;
    const char *hostname;
};


int collect_stats(int fd, void *data) {
    assert(fd != -1);
    assert(data != NULL);

    struct agent_context *context = (struct agent_context *)data;
    assert(context->sink != -1);
    assert(context->hostname != NULL);

    uint64_t v = 0;
    ssize_t r = read(fd, &v, sizeof(v));
    HANDLE_POSIX_RESULT(r, return -1, "collect_stats: read fd=%d", fd);
    HANDLE_RESULT(r != sizeof(v), return -1,
                  "collect_stats: read %zd bytes expected %zu", r, sizeof(v));
    HANDLE_RESULT(v != 1, (void)v,
                  "collect_stats: detected slow processing, "
                  "timer overrun %" PRIu64 " times", v);


    struct timespec ts;
    HANDLE_POSIX_RESULT(clock_gettime(CLOCK_REALTIME, &ts),
                        return -1, "collect_stats: clock_gettime");

    char message[MAX_MESSAGE_SIZE];
    for(const serializer *serializer = serializers;
        *serializer != NULL;
        ++serializer) {
        size_t messagelen = sizeof(message);
        HANDLE_RESULT((*serializer)(context->hostname, &ts, message, &messagelen) < 0,
                      goto NEXT_SERIALIZER, "collect_stats: serializer %p failed",
                      *serializer);
        assert(message[messagelen] == 0);
        HANDLE_POSIX_RESULT(send(context->sink, message, messagelen, 0),
                            (void)context->sink, "collect_stats: send");
NEXT_SERIALIZER:
        ;
    }
    return 0;
}


int run_agent(const char *hostname, const char *remote, const char *service) {
    assert(hostname != NULL);
    assert(remote != NULL);
    assert(service != NULL);

    int result = -1;
    int ev_loop = -1;
    struct agent_context context = {
        .sink = create_sink(remote, service),
        .hostname = hostname
    };
    HANDLE_RESULT(context.sink == -1,
                  goto CLEANUP, "can't connect to %s:%s", remote, service);

    struct event_handler timer = {
        .fd = -1,
        .handler = &collect_stats,
        .data = &context
    };

    HANDLE_RESULT((ev_loop = create_event_loop()) == -1,
                  goto CLEANUP, "can't initialize event loop");

    struct itimerspec timeout;
    timeout.it_interval.tv_sec = 1;
    timeout.it_interval.tv_nsec = 0;
    timeout.it_value.tv_sec = 0;
    timeout.it_value.tv_nsec = 1;
    HANDLE_RESULT(create_timer(ev_loop, &timeout, &timer) == -1,
                  goto CLEANUP, "can't create timer to query stats");

    result = run_event_loop(ev_loop);

CLEANUP:
    HANDLE_POSIX_RESULT(close(timer.fd), (void)timer, "fd=%d: close: timer", timer.fd);
    HANDLE_POSIX_RESULT(close(ev_loop), (void)ev_loop, "fd=%d: close: ev_loop", ev_loop);
    HANDLE_POSIX_RESULT(close(context.sink), (void)context.sink, "fd=%d: close: sink", context.sink);

    return result;
}
