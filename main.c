#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "agent.h"
#include "error_handling.h"

int main(int argc, char* argv[]) {
    openlog(basename(argv[0]), LOG_NDELAY | LOG_PERROR, LOG_USER);
    int result = EXIT_FAILURE;

    int hostnamelen = sysconf(_SC_HOST_NAME_MAX);
    HANDLE_POSIX_RESULT(hostnamelen,
                        hostnamelen = _POSIX_HOST_NAME_MAX,
                        "sysconf: _SC_HOST_NAME_MAX");
    char *hostname = malloc(hostnamelen + 1); // HOST_NAME_MAX does not include \0

    char *service = NULL;
    int opt = 0;

    HANDLE_POSIX_RESULT(gethostname(hostname, hostnamelen),
                        goto CLEANUP, "gethostname");
    hostname[hostnamelen] = 0;

    while ((opt = getopt(argc, argv, "p:")) != -1) {
        switch (opt) {
            case 'p':
                service = strdup(optarg);
                break;
            default: /* '?' */
                fprintf(stderr, "Usage: %s -p port hostname\n", argv[0]);
                goto CLEANUP;
        }
    }

    HANDLE_RESULT(service == NULL, goto CLEANUP, "Port not provided");
    HANDLE_RESULT(optind >= argc, goto CLEANUP, "Host not provided");

    syslog(LOG_INFO,
           "running at %s, sending metrics to %s:%s\n",
           hostname, argv[optind], service);

    result = run_agent(hostname, argv[optind], service);

CLEANUP:
    free(hostname); hostname = NULL;
    free(service); service = NULL;

    closelog();
    exit(result);
}
