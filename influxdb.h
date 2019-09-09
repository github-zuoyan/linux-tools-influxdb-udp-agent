#ifndef INFLUXDB_H_
#define INFLUXDB_H_

#include <sys/types.h>

int influxdb_serialize_memory_stat(const char *hostname,
                                   const struct timespec *ts,
                                   char *buf, size_t *buflen);

int influxdb_serialize_nic_stat(const char *hostname,
                                const struct timespec *ts,
                                char *buf, size_t *buflen);

int influxdb_serialize_proc_stat(char *stat, /* content of /proc/stat */
                                 const char *hostname,
                                 const struct timespec *ts,
                                 char *buf, size_t *buflen);

int influxdb_serialize_net_stat(char *stat, /* content of /proc/net/(netstat|snmp) */
                                const char **tags,
                                const char *hostname,
                                const struct timespec *ts,
                                char *buf, size_t *buflen);

#endif // INFLUXDB_H_
