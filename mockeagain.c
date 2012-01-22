#ifndef DDEBUG
#define DDEBUG 0
#endif

#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/poll.h>
#include <sys/uio.h>
#include <sys/time.h>
#include <dlfcn.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
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
static char  snd_timeout_fds[MAX_FD + 1];
static char **matchbufs = NULL;
static size_t matchbuf_len = 0;
static const char *pattern = NULL;
static int verbose = -1;


#   define init_libc_handle() \
        if (libc_handle == NULL) { \
            libc_handle = RTLD_NEXT; \
        }


typedef int (*poll_handle) (struct pollfd *ufds, unsigned int nfds,
    int timeout);
typedef ssize_t (*writev_handle) (int fildes, const struct iovec *iov,
    int iovcnt);
typedef int (*close_handle) (int fd);
typedef ssize_t (*send_handle) (int sockfd, const void *buf, size_t len,
    int flags);


static int get_verbose_level();
static void init_matchbufs();
static int now();


int
poll(struct pollfd *ufds, nfds_t nfds, int timeout)
{
    static void             *libc_handle;
    int                      retval;
    static poll_handle       orig_poll = NULL;
    struct pollfd           *p;
    int                      i;
    int                      fd;
    int                      begin = 0;
    int                      elapsed = 0;

    init_libc_handle();

    if (orig_poll == NULL) {
        orig_poll = dlsym(libc_handle, "poll");
        if (orig_poll == NULL) {
            fprintf(stderr, "mockeagain: could not find the underlying poll: "
                    "%s\n", dlerror());
            exit(1);
        }
    }

    init_matchbufs();

    dd("calling the original poll");

    if (pattern) {
        begin = now();
    }

    retval = (*orig_poll)(ufds, nfds, timeout);

    if (pattern) {
        elapsed = now() - begin;
    }

    if (retval > 0) {
        struct timeval  tm;

        p = ufds;
        for (i = 0; i < nfds; i++, p++) {
            fd = p->fd;
            if (fd > MAX_FD) {
                continue;
            }

            if (pattern && (p->revents & POLLOUT) && snd_timeout_fds[fd]) {

                if (get_verbose_level()) {
                    fprintf(stderr, "mockeagain: poll: should suppress write "
                            "event on fd %d.\n", fd);
                }

                p->revents &= ~POLLOUT;

                if (p->revents == 0) {
                    retval--;
                    continue;
                }
            }

            active_fds[fd] = p->revents;
            polled_fds[fd] = 1;
        }

        if (retval == 0) {
            if (get_verbose_level()) {
                fprintf(stderr, "mockeagain: poll: emulating timeout on "
                        "fd %d.\n", fd);
            }

            if (timeout < 0) {
                tm.tv_sec = 3600 * 24;
                tm.tv_usec = 0;

                if (get_verbose_level()) {
                    fprintf(stderr, "mockeagain: poll: sleeping 1 day "
                            "on fd %d.\n", fd);
                }

                select(0, NULL, NULL, NULL, &tm);

            } else {

                if (elapsed < timeout) {
                    int     diff;

                    diff = timeout - elapsed;

                    tm.tv_sec = diff / 1000;
                    tm.tv_usec = diff % 1000 * 1000;

                    if (get_verbose_level()) {
                        fprintf(stderr, "mockeagain: poll: sleeping %d ms "
                                "on fd %d.\n", diff, fd);
                    }

                    select(0, NULL, NULL, NULL, &tm);
                }
            }
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
    size_t                   len;

    if (fd <= MAX_FD && polled_fds[fd] && !(active_fds[fd] & POLLOUT)) {
        if (get_verbose_level()) {
            fprintf(stderr, "mockeagain: mocking \"writev\" on fd %d to "
                    "signal EAGAIN.\n", fd);
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

        len = 0;
        p = iov;
        for (i = 0; i < iovcnt; i++, p++) {
            len += p->iov_len;
        }
    }

    if (new_iov[0].iov_base == NULL) {
        retval = (*orig_writev)(fd, iov, iovcnt);

    } else {
        if (get_verbose_level()) {
            fprintf(stderr, "mockeagain: mocking \"writev\" on fd %d to emit "
                    "1 of %llu bytes.\n", fd, (unsigned long long) len);
        }

        if (pattern) {
            char          *p;
            size_t         len;
            char           c;

            c = *(char *) new_iov[0].iov_base;

            if (matchbufs[fd] == NULL) {

                matchbufs[fd] = malloc(matchbuf_len);
                if (matchbufs[fd] == NULL) {
                    fprintf(stderr, "mockeagain: ERROR: failed to allocate memory.\n");
                }

                p = matchbufs[fd];
                memset(p, 0, matchbuf_len);

                p[0] = c;

                len = 1;

            } else {
                p = matchbufs[fd];

                len = strlen(p);

                if (len < matchbuf_len - 1) {
                    p[len] = c;
                    len++;

                } else {
                    memmove(p, p + 1, matchbuf_len - 2);

                    p[matchbuf_len - 2] = c;
                }
            }

            /* test if the pattern matches the matchbuf */

            dd("matchbuf: %.*s (len: %d)", (int) len, p,
                    (int) matchbuf_len - 1);

            if (len == matchbuf_len - 1 && strncmp(p, pattern, len) == 0) {
                if (get_verbose_level()) {
                    fprintf(stderr, "mockeagain: \"writev\" has found a match for "
                            "the timeout pattern \"%s\" on fd %d.\n", pattern, fd);
                }

                snd_timeout_fds[fd] = 1;
            }
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

        if (matchbufs && matchbufs[fd]) {
            free(matchbufs[fd]);
            matchbufs[fd] = NULL;
        }

        active_fds[fd] = 0;
        polled_fds[fd] = 0;
        snd_timeout_fds[fd] = 0;
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

    if (verbose >= 0) {
        return verbose;
    }

    p = getenv("MOCKEAGAIN_VERBOSE");
    if (p == NULL || *p == '\0') {
        dd("verbose env empty");
        verbose = 0;
        return verbose;
    }

    if (*p >= '0' && *p <= '9') {
        dd("verbose env value: %s", p);
        verbose = *p - '0';
        return verbose;
    }

    dd("bad verbose env value: %s", p);
    verbose = 0;
    return verbose;
}


static void
init_matchbufs()
{
    const char          *p;
    int                  len;

    if (matchbufs != NULL) {
        return;
    }

    p = getenv("MOCKEAGAIN_WRITE_TIMEOUT_PATTERN");
    if (p == NULL || *p == '\0') {
        dd("write_timeout env empty");
        return;
    }

    len = strlen(p);

    matchbufs = malloc((MAX_FD + 1) * sizeof(char *));
    if (matchbufs == NULL) {
        fprintf(stderr, "mockeagain: ERROR: failed to allocate memory.\n");
        return;
    }

    memset(matchbufs, 0, (MAX_FD + 1) * sizeof(char *));
    matchbuf_len = len + 1;

    pattern = p;

    if (get_verbose_level()) {
        fprintf(stderr, "mockeagain: reading write timeout pattern: %s\n",
            pattern);
    }
}


/* returns a time in milliseconds */
static int now() {
   struct timeval tv;

   gettimeofday(&tv, NULL);

   return tv.tv_sec % (3600 * 24) + tv.tv_usec/1000;
}

