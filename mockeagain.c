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


#define MAX_FDS 1024


static void *libc_handle = NULL;
static short active_fds[MAX_FDS];
static char polled_fds[MAX_FDS];


#if defined(RTLD_NEXT)
#   define init_libc_handle() \
        if (libc_handle == NULL) { \
            libc_handle = RTLD_NEXT; \
        }
#else
#   define init_libc_handle() \
        libc_handle = dlopen("libc.so.6", RTLD_LAZY); \
        if (libc_handle == NULL) { \
            fprintf(stderr, "slowrites: could not load libc.so.6: %s\n", \
                    dlerror()); \
            exit(1); \
        }
#endif


typedef int (*poll_handle) (struct pollfd *ufds, unsigned int nfds,
    int timeout);


typedef ssize_t (*writev_handle) (int fildes, const struct iovec *iov,
    int iovcnt);


typedef int (*close_handle) (int fd);


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
            fprintf(stderr, "slowrites: could not find the underlying poll: "
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
            if (fd > MAX_FDS) {
                continue;
            }
            active_fds[fd - 1] = p->revents;
            polled_fds[fd - 1] = 1;
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

    if (fd <= MAX_FDS && polled_fds[fd - 1] && !(active_fds[fd - 1] & POLLOUT)) {
        errno = EAGAIN;
        return -1;
    }

    init_libc_handle();

    if (orig_writev == NULL) {
        orig_writev = dlsym(libc_handle, "writev");
        if (orig_writev == NULL) {
            fprintf(stderr, "slowrites: could not find the underlying writev: "
                    "%s\n", dlerror());
            exit(1);
        }
    }

    if (fd <= MAX_FDS && polled_fds[fd - 1]) {
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
        dd("calling the original writev on fd %d", fd);
        retval = (*orig_writev)(fd, new_iov, 1);
        active_fds[fd - 1] &= ~POLLOUT;
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
            fprintf(stderr, "slowrites: could not find the underlying close: "
                    "%s\n", dlerror());
            exit(1);
        }
    }

    if (fd <= MAX_FDS) {
        if (polled_fds[fd - 1]) {
            dd("calling the original close on fd %d", fd);
        }

        active_fds[fd - 1] = 0;
        polled_fds[fd - 1] = 0;
    }

    retval = (*orig_close)(fd);

    return retval;
}

