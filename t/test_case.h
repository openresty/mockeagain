#ifndef TEST_CASE_H
#define TEST_CASE_H

#define _GNU_SOURCE
#include <sys/socket.h>

#if __linux__
#include <sys/epoll.h>
#endif

#include <stdlib.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

int run_test(int fd);

enum {
    MOCKING_READS = 0x01,
    MOCKING_WRITES = 0x02
};

int set_mocking(int types);

int set_write_timeout_pattern(const char *pattern);

#endif /* !TEST_CASE_H */
