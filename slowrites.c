#define DDEBUG 1

#define _GNU_SOURCE
#include <sys/poll.h>
#include <sys/uio.h>
#include <dlfcn.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>

#if DDEBUG
#   define dd(...) \
        fprintf(stderr, __VA_ARGS__); \
        fprintf(stderr, " at %s line %d.\n", __FILE__, __LINE__)
#else
#   define dd(...)
#endif


static void *libc_handle = NULL;


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

    return retval;
}


ssize_t
writev(int fd, const struct iovec *iov, int iovcnt)
{
    ssize_t                  retval;
    static writev_handle     orig_writev = NULL;

    init_libc_handle();

    if (orig_writev == NULL) {
        orig_writev = dlsym(libc_handle, "writev");
        if (orig_writev == NULL) {
            fprintf(stderr, "slowrites: could not find the underlying writev: "
                    "%s\n", dlerror());
            exit(1);
        }
    }

    dd("calling the original writev on fd %d", fd);

    retval = (*orig_writev)(fd, iov, iovcnt);

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

    dd("calling the original close on fd %d", fd);

    retval = (*orig_close)(fd);

    return retval;
}

