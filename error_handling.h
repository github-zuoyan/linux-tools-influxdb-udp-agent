#ifndef ERROR_HANDLING_H_
#define ERROR_HANDLING_H_

#include <errno.h>
#include <string.h>

#define HANDLE_RESULT(condition, handler, format, ...)      \
    do {                                                    \
        if((condition)) {                                   \
            if(format != NULL) {                            \
                syslog(LOG_ERR, format,  ##__VA_ARGS__);    \
            }                                               \
            handler;                                        \
        }                                                   \
    } while(0);                                             \


#define HANDLE_POSIX_RESULT(statement, handler, prefix, ...)    \
    do {                                                        \
        char buf[256];                                          \
        int ec = errno;                                         \
                                                                \
        HANDLE_RESULT((statement) == -1,                        \
                      handler,                                  \
                      prefix ": %s\n",                          \
                      ##__VA_ARGS__,                            \
                      strerror_r(ec, buf, sizeof(buf)));        \
    } while(0);                                                 \


#endif // ERROR_HANDLING_H_
