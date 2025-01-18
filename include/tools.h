#ifndef TOOLS_H
#define TOOLS_H
#include "rdma_comm.h"
#pragma once
#include <infiniband/verbs.h>
#include <pthread.h>
#include <stdio.h>


#define SIZE 16

#define Linesize 80
static int verbose_log = 3;
static int verbose_out = 3;
extern FILE *logfile;
extern pthread_t rdma_server_tid;
extern pthread_t rdma_client_tid;
extern pthread_t rdma_listen_tid;
extern char errstr[Linesize];
static const char *clientstr = "[Thread client]";
static const char *serverstr = "[Thread server]";
static const char *listenstr = "[Thread listen]";
static const char *mainstr = "[Thread  main ]";

#define VERBOSE_LOG(level, fmt, ...)                                                               \
    if (verbose_log >= level) {                                                                    \
        if (logfile != NULL)                                                                       \
            fprintf(logfile, fmt, ##__VA_ARGS__);                                                  \
        else                                                                                       \
            fprintf(stdout, fmt, ##__VA_ARGS__);                                                   \
    }

#define VERBOSE_OUT(level, fmt, ...)                                                               \
    if (verbose_out >= level) {                                                                    \
        printf(fmt, ##__VA_ARGS__);                                                                \
    }

#define show_errno() (errno == 0 ? "None" : strerror(errno))

#define log_err(STR, ...)                                                                          \
    do {                                                                                           \
        const char *str;                                                                           \
        if (pthread_self() == rdma_client_tid) {                                                   \
            str = clientstr;                                                                       \
        } else if (pthread_self() == rdma_server_tid) {                                            \
            str = serverstr;                                                                       \
        } else if (pthread_self() == rdma_listen_tid) {                                            \
            str = listenstr;                                                                       \
        } else {                                                                                   \
            str = mainstr;                                                                         \
        }                                                                                          \
        fprintf(logfile, "[\033[31mERROR\033[0m] %s (%s:%d:%s: errno: %s) " STR "\n", str,         \
                __FILE__, __LINE__, __func__, show_errno(), ##__VA_ARGS__);                        \
    } while (0)

#define log_info(level, STR, ...)                                                                  \
    if (verbose_log >= level) {                                                                    \
        const char *str;                                                                           \
        if (pthread_self() == rdma_client_tid) {                                                   \
            str = clientstr;                                                                       \
        } else if (pthread_self() == rdma_server_tid) {                                            \
            str = serverstr;                                                                       \
        } else if (pthread_self() == rdma_listen_tid) {                                            \
            str = listenstr;                                                                       \
        } else {                                                                                   \
            str = mainstr;                                                                         \
        }                                                                                          \
        fprintf(logfile, "[INFO] %s (%s:%d:%s) " STR "\n", str, __FILE__, __LINE__, __func__,      \
                ##__VA_ARGS__);                                                                    \
    }

#define log_out(level, STR, ...)                                                                   \
    if (verbose_out >= level) {                                                                    \
        fprintf(stdout, "[INFO] (%s:%d:%s) " STR "\n", __FILE__, __LINE__, __func__,               \
                ##__VA_ARGS__);                                                                    \
    }

#define cond_check(COND, STR, ...)                                                                 \
    if (!(COND)) {                                                                                 \
        log_err(STR, ##__VA_ARGS__);                                                               \
        exit(EXIT_FAILURE);                                                                        \
    }

static inline int open_logfile(char *filename) {
    logfile = fopen(filename, "w+");
    if (logfile == NULL) {
        printf("Cannot open logfile %s\n", filename);
        return -1;
    }
    return 0;
}

#endif /* TOOLS_H */