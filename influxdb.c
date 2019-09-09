#include "influxdb.h"

#include <linux/if_link.h>
#include <net/if.h>
#include <sys/sysinfo.h>
#include <sys/types.h>

#include <assert.h>
#include <inttypes.h>
#include <ifaddrs.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <unistd.h>

#include "error_handling.h"

int format(char **buf, size_t *buflen, const char *format, ...) {
    assert(buf != NULL);
    assert(*buf != NULL);
    assert(buflen != NULL);
    assert(format != NULL);

    va_list vl;
    va_start(vl, format);
    ssize_t result = vsnprintf(*buf, *buflen, format, vl);
    va_end(vl);

    HANDLE_POSIX_RESULT(result, return -1, "format: snprintf");
    HANDLE_RESULT((size_t)result >= *buflen, return -1,
                  "format: buffer too small: "
                  "has %zu need %zd bytes\n", *buflen, result);
    *buflen -= result;
    *buf += result;
    assert(**buf == 0);
    return 0;
}


int influxdb_serialize_memory_stat(const char *hostname,
                                   const struct timespec *ts,
                                   char *buf, size_t *buflen) {
    assert(hostname != NULL);
    assert(ts != NULL);
    assert(buf != NULL);
    assert(buflen != NULL);

    struct sysinfo si;
    HANDLE_POSIX_RESULT(sysinfo(&si), return -1,
                        "influxdb_serialize_memory_stat: sysinfo");

    size_t len = *buflen;
    HANDLE_RESULT(format(&buf, &len,
                         "memory,hostname=%s,unit=%di "
                         "totalram=%lui,freeram=%lui,sharedram=%lui,bufferram=%lui,"
                         "totalswap=%lui,freeswap=%lui,"
                         "totalhigh=%lui,freehigh=%lui "
                         "%ld%09ld\n",
                         hostname, si.mem_unit,
                         si.totalram, si.freeram, si.sharedram, si.bufferram,
                         si.totalswap, si.freeswap,
                         si.totalhigh, si.freehigh,
                         ts->tv_sec, ts->tv_nsec) == -1,
                  return -1, "influxdb_serialize_memory_stat: format");
    assert(*buf == 0);
    *buflen -= len;

    /* struct sysinfo { */
    /*         long uptime;             /\* Seconds since boot *\/ */
    /*         unsigned long loads[3];  /\* 1, 5, and 15 minute load averages *\/ */
    /*         unsigned long totalram;  /\* Total usable main memory size *\/ */
    /*         unsigned long freeram;   /\* Available memory size *\/ */
    /*         unsigned long sharedram; /\* Amount of shared memory *\/ */
    /*         unsigned long bufferram; /\* Memory used by buffers *\/ */
    /*         unsigned long totalswap; /\* Total swap space size *\/ */
    /*         unsigned long freeswap;  /\* swap space still available *\/ */
    /*         unsigned short procs;    /\* Number of current processes *\/ */
    /*         unsigned long totalhigh; /\* Total high memory size *\/ */
    /*         unsigned long freehigh;  /\* Available high memory size *\/ */
    /*         unsigned int mem_unit;   /\* Memory unit size in bytes *\/ */
    /*         char _f[20-2*sizeof(long)-sizeof(int)]; /\* Padding to 64 bytes *\/ */
    /*     }; */
    return 0;
}

int influxdb_serialize_nic_stat(const char *hostname,
                                const struct timespec *ts,
                                char *buf, size_t *buflen) {
    assert(hostname != NULL);
    assert(ts != NULL);
    assert(buf != NULL);
    assert(buflen != NULL);

    struct ifaddrs *ifa = NULL;
    HANDLE_POSIX_RESULT(getifaddrs(&ifa), goto IFADDRS_CLEANUP,
                        "influxdb_serialize_nic_stat: getifaddrs");
    assert(ifa != NULL);
    size_t offset = 0;
    for (struct ifaddrs *cifa = ifa; cifa != NULL; cifa = cifa->ifa_next) {
        char *b = buf + offset;
        size_t blen = *buflen - offset;
        assert(cifa != NULL);
        if(cifa->ifa_flags & IFF_LOOPBACK) continue;
        if(!(cifa->ifa_flags & IFF_UP && cifa->ifa_flags & IFF_RUNNING)) continue;
        if(cifa->ifa_addr == NULL || cifa->ifa_addr->sa_family != AF_PACKET) continue;
        if(cifa->ifa_data == NULL) continue;
        struct rtnl_link_stats *stats = cifa->ifa_data;

        HANDLE_RESULT(format(&b, &blen,
                             "nic,hostname=%s,if=%s "
                             "rx_packets=%ui,tx_packets=%ui,rx_bytes=%ui,tx_bytes=%ui,"
                             "rx_errors=%ui,tx_errors=%ui,rx_dropped=%ui,tx_dropped=%ui,"
                             "multicast=%ui,collisions=%ui,"
                             "rx_length_errors=%ui,rx_over_errors=%ui,rx_crc_errors=%ui,"
                             "rx_frame_errors=%ui,rx_fifo_errors=%ui,rx_missed_errors=%ui,"
                             "tx_aborted_errors=%ui,tx_carrier_errors=%ui,tx_fifo_errors=%ui,"
                             "tx_heartbeat_errors=%ui,tx_window_errors=%ui "
                             "%ld%09ld\n",
                             hostname, cifa->ifa_name,
                             stats->rx_packets, stats->tx_packets, stats->rx_bytes, stats->tx_bytes,
                             stats->rx_errors, stats->tx_errors, stats->rx_dropped, stats->tx_dropped,
                             stats->multicast, stats->collisions,
                             stats->rx_length_errors, stats->rx_over_errors, stats->rx_crc_errors,
                             stats->rx_frame_errors, stats->rx_fifo_errors, stats->rx_missed_errors,
                             stats->tx_aborted_errors, stats->tx_carrier_errors, stats->tx_fifo_errors,
                             stats->tx_heartbeat_errors, stats->tx_window_errors,
                             ts->tv_sec, ts->tv_nsec) == -1,
                      (void)stats, "influxdb_serialize_nic_stat: format");
        assert(*b == 0);
        offset = *buflen - blen;
        assert(buf[offset] == 0);

/*
        struct rtnl_link_stats {
        __u32   rx_packets;             / * total packets received       * /
        __u32   tx_packets;             / * total packets transmitted    * /
        __u32   rx_bytes;               / * total bytes received         * /
        __u32   tx_bytes;               / * total bytes transmitted      * /
        __u32   rx_errors;              / * bad packets received         * /
        __u32   tx_errors;              / * packet transmit problems     * /
        __u32   rx_dropped;             / * no space in linux buffers    * /
        __u32   tx_dropped;             / * no space available in linux  * /
        __u32   multicast;              / * multicast packets received   * /
        __u32   collisions;

        / * detailed rx_errors: * /
        __u32   rx_length_errors;
        __u32   rx_over_errors;         / * receiver ring buff overflow  * /
        __u32   rx_crc_errors;          / * recved pkt with crc error    * /
        __u32   rx_frame_errors;        / * recv'd frame alignment error * /
        __u32   rx_fifo_errors;         / * recv'r fifo overrun          * /
        __u32   rx_missed_errors;       / * receiver missed packet       * /

        / * detailed tx_errors * /
        __u32   tx_aborted_errors;
        __u32   tx_carrier_errors;
        __u32   tx_fifo_errors;
        __u32   tx_heartbeat_errors;
        __u32   tx_window_errors;

        / * for cslip etc * /
        __u32   rx_compressed;
        __u32   tx_compressed;

        __u32   rx_nohandler;           / * dropped, no handler found    * /
};
*/
    }
    assert(buf[offset] == 0);
    *buflen = offset;

IFADDRS_CLEANUP:
    freeifaddrs(ifa);
    return 0;
}


int influxdb_serialize_cpu_stat(char *stat,
                                const char *hostname,
                                char *buf, size_t *buflen) {
    assert(stat != NULL);
    assert(hostname != NULL);
    assert(buf != NULL);
    assert(buflen != NULL);

    static const char *tag = "cpu";
    static const char *names[] = {
        "user",
        "nice",
        "system",
        "idle",
        "iowait",
        "irq",
        "softirq",
        "steal",
        "guest",
        "guest_nice",
        NULL
    };

    /* TODO: think how to get rid of this call */
    const long USER_HZ = sysconf(_SC_CLK_TCK);
    HANDLE_POSIX_RESULT(USER_HZ,
                        return -1, "influxdb_serialize_cpu_stat: sysconf(_SC_CLK_TCK)");

    size_t len = *buflen;
    char *stash = NULL, *token = strtok_r(stat, " ", &stash);
    HANDLE_RESULT(format(&buf, &len, ",cpu=%s",
                         *(token + strlen(tag)) == 0 ? "all" : token + strlen(tag)) == -1,
                  return -1, "influxdb_serialize_cpu_stat: cpu tag");
    HANDLE_RESULT(format(&buf, &len, ",hostname=%s,user_hz=%ldi ", hostname, USER_HZ) == -1,
                  return -1, "influxdb_serialize_cpu_stat: tags");

    uint64_t total = 0;
    for(const char **name = names, *token = strtok_r(NULL, " ", &stash);
        token != NULL && name != NULL;
        token = strtok_r(NULL, " ", &stash), ++name) {

        char *err = NULL;
        uint64_t value = strtoull(token, &err, 10);
        HANDLE_RESULT(*err != 0, return -1, "influxdb_serialize_cpu_stat: strtoull");
        total += value;

        HANDLE_RESULT(format(&buf, &len, "%s=%si,", *name, token) == -1,
                      return -1, "influxdb_serialize_cpu_stat: values");
    }
    HANDLE_RESULT(format(&buf, &len, "total=%" PRIu64 "i ", total) == -1,
                  return -1, "influxdb_serialize_cpu_stat: total");

    *buflen -= len;
    return 0;
}

int influxdb_serialize_ctx_stat(char *stat,
                                const char *hostname,
                                char *buf, size_t *buflen) {
    assert(stat != NULL);
    assert(hostname != NULL);
    assert(buf != NULL);
    assert(buflen != NULL);

    size_t len = *buflen;
    HANDLE_RESULT(format(&buf, &len, ",hostname=%s ", hostname) == -1,
                  return -1, "influxdb_serialize_ctx_stat: tags");

    char *stash = NULL, *token = strtok_r(stat, " ", &stash);
    token = strtok_r(NULL, " ", &stash);
    HANDLE_RESULT(token == NULL,
                  return -1, "influxdb_serialize_ctx_stat: invalid format");
    HANDLE_RESULT(format(&buf, &len, "count=%s ", token) == -1,
                  return -1, "influxdb_serialize_ctx_stat: value");
    *buflen -= len;
    return 0;
}

int influxdb_serialize_irq_stat(char *stat,
                                const char *hostname,
                                char *buf, size_t *buflen) {
    assert(stat != NULL);
    assert(hostname != NULL);
    assert(buf != NULL);
    assert(buflen != NULL);

    size_t len = *buflen;
    HANDLE_RESULT(format(&buf, &len, ",hostname=%s ", hostname) == -1,
                  return -1, "influxdb_serialize_irq_stat: tags");
    assert(*buf == 0);
    char *stash = NULL, *token = strtok_r(stat, " ", &stash);
    token = strtok_r(NULL, " ", &stash);
    HANDLE_RESULT(token == NULL,
                  return -1, "influxdb_serialize_irq_stat: invalid format");
    HANDLE_RESULT(format(&buf, &len, "count=%s,", token) == -1,
                  return -1, "influxdb_serialize_irq_stat: total");
    assert(*buf == 0);

    size_t idx = 0;
    for(token = strtok_r(NULL, " ", &stash);
        token != NULL;
        token = strtok_r(NULL, " ", &stash), ++idx) {

        if(strncmp(token, "0", sizeof("0")) == 0) continue;
        HANDLE_RESULT(format(&buf, &len, "irq%zd=%s,", idx, token) == -1,
                      return -1, "influxdb_serialize_irq_stat: irq%zd", idx);
        assert(*buf == 0);
    }

    if(*(buf - 1) == ',') *(buf - 1) = ' ';
    *buflen -= len;
    return 0;
}

int influxdb_serialize_nop(char *stat,
                           const char *hostname,
                           char *buf, size_t *buflen) {
    assert(stat != NULL);
    (void)stat;
    assert(hostname != NULL);
    (void)hostname;
    assert(buf != NULL);
    (void)buf;
    assert(buflen != NULL);
    (void)buflen;
    return -1;
}


int influxdb_serialize_proc_stat(char *stat,
                                 const char *hostname,
                                 const struct timespec *ts,
                                 char *buf, size_t *buflen) {
    assert(stat != NULL);
    assert(hostname != NULL);
    assert(ts != NULL);
    assert(buf != NULL);
    assert(buflen != NULL);

    struct serializer {
        const char* tag;
        int(*handler)(char *stat,
                      const char *hostname,
                      char *buf, size_t *buflen);
    };

    static const struct serializer serializer[] = {
        {
            .tag = "cpu",
            .handler = &influxdb_serialize_cpu_stat
        },
        {
            .tag = "intr",
            .handler = &influxdb_serialize_irq_stat
        },
        {
            .tag = "ctxt",
            .handler = &influxdb_serialize_ctx_stat
        },
        {
            .tag = "softirq",
            .handler = &influxdb_serialize_irq_stat
        },
        {
            .tag = NULL,
            .handler = NULL
        }
    };

    size_t offset = 0;
    for(char *stash = NULL, *token = strtok_r(stat, "\n", &stash);
        token != NULL;
        token = strtok_r(NULL, "\n", &stash)) {
        for(const struct serializer *s = serializer; s->tag != NULL; ++s) {
            if(strncmp(token, s->tag, strlen(s->tag)) == 0) {
                char *b = buf + offset;
                size_t blen = *buflen - offset;
                HANDLE_RESULT(format(&b, &blen, "%s", s->tag) == -1,
                              goto NEXT_TOKEN,
                              "influxdb_serialize_proc_stat[%s]: "
                              "failed to serialize tag", s->tag);
                assert(*b == 0);
                size_t clen = blen;
                HANDLE_RESULT((*s->handler)(token, hostname, b, &clen) == -1,
                              goto NEXT_TOKEN,
                              "influxdb_serialize_proc_stat[%s]: "
                              "failed to serialize content", s->tag);
                b += clen;
                blen -= clen;
                assert(*b == 0);
                HANDLE_RESULT(format(&b, &blen, "%ld%09ld\n",
                                     ts->tv_sec, ts->tv_nsec) == -1,
                              goto NEXT_TOKEN,
                              "influxdb_serialize_proc_stat[%s]: "
                              "failed to serialize timestamp", s->tag);
                assert(*b == 0);
                offset = *buflen - blen;
                assert(buf[offset] == 0);
        NEXT_TOKEN:
                buf[offset] = 0;
            }
        }
    }
    assert(buf[offset] == 0);
    *buflen = offset;
    return 0;
}


int influxdb_serialize_kv(char *keys, char *values,
                          const char *hostname,
                          char *buf, size_t *buflen) {
    assert(keys != NULL);
    assert(values != NULL);
    assert(hostname != NULL);
    assert(buf != NULL);
    assert(buflen != NULL);

    size_t len = *buflen;

    char *kstash = NULL;
    char *vstash = NULL;

    char *ktag = strtok_r(keys, ": ", &kstash);
    char *vtag = strtok_r(values, ": ", &vstash);
    HANDLE_RESULT(ktag == NULL || vtag == NULL || strcmp(ktag, vtag) != 0,
                  return -1, "influxdb_serialize_kv: tag mismatch - "
                  "key=%s, value=%s\n", ktag, vtag);

    HANDLE_RESULT(format(&buf, &len, "%s,hostname=%s ", ktag, hostname) == -1,
                  return -1, "influxdb_serialize_kv: tag=%s", ktag);
    assert(*buf == 0);

    char *key = strtok_r(NULL, " ", &kstash);
    char *value = strtok_r(NULL, " ", &vstash);
    while(key != NULL && value != NULL) {
        HANDLE_RESULT(format(&buf, &len, "%s=%s,", key, value) == -1,
                      return -1, "influxdb_serialize_kv: %s=%s", key, value);
        assert(*buf == 0);
        key = strtok_r(NULL, " ", &kstash);
        value = strtok_r(NULL, " ", &vstash);
    }

    HANDLE_RESULT(key != NULL,
                  return -1, "influxdb_serialize_kv: too many keys");
    HANDLE_RESULT(value != NULL,
                  return -1, "influxdb_serialize_kv: too many values");

    assert(*buf == 0);
    if(*(buf - 1) == ',') *(buf - 1) = ' ';
    *buflen -= len;
    return 0;
}

int influxdb_serialize_net_stat(char *stat,
                                const char **tags,
                                const char *hostname,
                                const struct timespec *ts,
                                char *buf, size_t *buflen) {
    assert(stat != NULL);
    assert(tags != NULL);
    assert(hostname != NULL);
    assert(ts != NULL);
    assert(buf != NULL);
    assert(buflen != NULL);

    size_t offset = 0;
    char *stash = NULL;
    for(char *names = strtok_r(stat, "\n", &stash), *values = strtok_r(NULL, "\n", &stash);
        names != NULL && values != NULL;
        names = strtok_r(NULL, "\n", &stash), values = strtok_r(NULL, "\n", &stash)) {
        for(const char **tag = tags; *tag != NULL; ++tag) {
            if(strncmp(names, *tag, strlen(*tag)) != 0 || *(names + strlen(*tag)) != ':') {
                continue;
            }
            char *b = buf + offset;
            size_t blen = *buflen - offset;
            size_t clen = blen;
            HANDLE_RESULT(influxdb_serialize_kv(names, values, hostname, b, &clen) == -1,
                          goto NEXT_TOKEN,
                          "influxdb_serialize_net_stat[%s]: "
                          "failed to serialize content", *tag);
            b += clen;
            blen -= clen;
            assert(*b == 0);
            HANDLE_RESULT(format(&b, &blen, "%ld%09ld\n",
                                 ts->tv_sec, ts->tv_nsec) == -1,
                          goto NEXT_TOKEN,
                          "influxdb_serialize_net_stat[%s]: "
                          "failed to serialize timestamp", *tag);
            assert(*b == 0);
            offset = *buflen - blen;
            assert(buf[offset] == 0);
        NEXT_TOKEN:
            buf[offset] = 0;
        }
    }
    assert(buf[offset] == 0);
    *buflen = offset;
    return 0;
}
