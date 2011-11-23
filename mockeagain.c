#ifndef DDEBUG
#define DDEBUG 0
#endif

#define _GNU_SOURCE
#include <sys/poll.h>
#include <sys/uio.h>
#include <dlfcn.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#if DDEBUG
#   define dd(...) \
        fprintf(stderr, __VA_ARGS__); \
        fprintf(stderr, " at %s line %d.\n", __FILE__, __LINE__)
#else
#   define dd(...)
#endif


#define MAX_FD 1024


static void *libc_handle = NULL;
static short active_fds[MAX_FD + 1];
static char  polled_fds[MAX_FD + 1];


#if defined(RTLD_NEXT)
#   define init_libc_handle() \
        if (libc_handle == NULL) { \
            libc_handle = RTLD_NEXT; \
        }
#else
#   define init_libc_handle() \
        libc_handle = dlopen("libc.so.6", RTLD_LAZY); \
        if (libc_handle == NULL) { \
            fprintf(stderr, "mockeagain: could not load libc.so.6: %s\n", \
                    dlerror()); \
            exit(1); \
        }
#endif


typedef int (*poll_handle) (struct pollfd *ufds, unsigned int nfds,
    int timeout);
typedef ssize_t (*writev_handle) (int fildes, const struct iovec *iov,
    int iovcnt);
typedef int (*close_handle) (int fd);
typedef ssize_t (*send_handle) (int sockfd, const void *buf, size_t len,
    int flags);


static int get_verbose_level();


int
poll(struct pollfd *ufds, nfds_t nfds, int timeout)
{
    static void             *libc_handle;
    int                      retval;
    static poll_handle       orig_poll = NULL;
    struct pollfd           *p;
    int                      i;
    int                      fd;

    init_libc_handle();

    if (orig_poll == NULL) {
        orig_poll = dlsym(libc_handle, "poll");
        if (orig_poll == NULL) {
            fprintf(stderr, "mockeagain: could not find the underlying poll: "
                    "%s\n", dlerror());
            exit(1);
        }
    }

    dd("calling the original poll");

    retval = (*orig_poll)(ufds, nfds, timeout);

    if (retval > 0) {
        p = ufds;
        for (i = 0; i < nfds; i++, p++) {
            fd = p->fd;
            if (fd > MAX_FD) {
                continue;
            }
            active_fds[fd] = p->revents;
            polled_fds[fd] = 1;
        }
    }

    return retval;
}


ssize_t
writev(int fd, const struct iovec *iov, int iovcnt)
{
    ssize_t                  retval;
    static writev_handle     orig_writev = NULL;
    struct iovec             new_iov[1] = { {NULL, 0} };
    const struct iovec      *p;
    int                      i;

    if (fd <= MAX_FD && polled_fds[fd] && !(active_fds[fd] & POLLOUT)) {
        if (get_verbose_level()) {
            fprintf(stderr, "mockeagain: mocking \"writev\" on fd %d to "
                    "signal EAGAIN\n", fd);
        }

        errno = EAGAIN;
        return -1;
    }

    init_libc_handle();

    if (orig_writev == NULL) {
        orig_writev = dlsym(libc_handle, "writev");
        if (orig_writev == NULL) {
            fprintf(stderr, "mockeagain: could not find the underlying writev: "
                    "%s\n", dlerror());
            exit(1);
        }
    }

    if (fd <= MAX_FD && polled_fds[fd]) {
        p = iov;
        for (i = 0; i < iovcnt; i++, p++) {
            if (p->iov_base == NULL || p->iov_len == 0) {
                continue;
            }

            new_iov[0].iov_base = p->iov_base;
            new_iov[0].iov_len = 1;
            break;
        }
    }

    if (new_iov[0].iov_base == NULL) {
        retval = (*orig_writev)(fd, iov, iovcnt);

    } else {
        if (get_verbose_level()) {
            fprintf(stderr, "mockeagain: mocking \"writev\" on fd %d to emit "
                    "1 byte of data only\n", fd);
        }

        dd("calling the original writev on fd %d", fd);
        retval = (*orig_writev)(fd, new_iov, 1);
        active_fds[fd] &= ~POLLOUT;
    }

    return retval;
}


int
close(int fd)
{
    int                     retval;
    static close_handle     orig_close = NULL;

    init_libc_handle();

    if (orig_close == NULL) {
        orig_close = dlsym(libc_handle, "close");
        if (orig_close == NULL) {
            fprintf(stderr, "mockeagain: could not find the underlying close: "
                    "%s\n", dlerror());
            exit(1);
        }
    }

    if (fd <= MAX_FD) {
        if (polled_fds[fd]) {
            dd("calling the original close on fd %d", fd);
        }

        active_fds[fd] = 0;
        polled_fds[fd] = 0;
    }

    retval = (*orig_close)(fd);

    return retval;
}


ssize_t
send(int fd, const void *buf, size_t len, int flags)
{
    ssize_t                  retval;
    static send_handle       orig_send = NULL;

    if (fd <= MAX_FD && polled_fds[fd] && !(active_fds[fd] & POLLOUT)) {
        if (get_verbose_level()) {
            fprintf(stderr, "mockeagain: mocking \"send\" on fd %d to "
                    "signal EAGAIN\n", fd);
        }

        errno = EAGAIN;
        return -1;
    }

    init_libc_handle();

    if (orig_send == NULL) {
        orig_send = dlsym(libc_handle, "send");
        if (orig_send == NULL) {
            fprintf(stderr, "mockeagain: could not find the underlying send: "
                    "%s\n", dlerror());
            exit(1);
        }
    }

    if (fd <= MAX_FD && polled_fds[fd] && len) {
        if (get_verbose_level()) {
            fprintf(stderr, "mockeagain: mocking \"send\" on fd %d to emit "
                    "1 byte of data only\n", fd);
        }

        dd("calling the original send on fd %d", fd);

        retval = (*orig_send)(fd, buf, 1, flags);
        active_fds[fd] &= ~POLLOUT;

    } else {
        retval = (*orig_send)(fd, buf, len, flags);
    }

    return retval;
}


static int
get_verbose_level()
{
    const char          *p;

    p = getenv("MOCKEAGAIN_VERBOSE");
    if (p == NULL || *p == '\0') {
        dd("verbose env empty");
        return 0;
    }

    if (*p >= '0' && *p <= '9') {
        dd("verbose env value: %s", p);
        return *p - '0';
    }

    dd("bad verbose env value: %s", p);
    return 0;
}

