#ifndef DDEBUG
#define DDEBUG 0
#endif

#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <sys/uio.h>
#include <sys/time.h>
#include <dlfcn.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#if __linux__
#include <sys/eventfd.h>
#include <sys/signalfd.h>
#endif

#if DDEBUG
#   define dd(...) \
        fprintf(stderr, "mockeagain: "); \
        fprintf(stderr, __VA_ARGS__); \
        fprintf(stderr, " at %s line %d.\n", __FILE__, __LINE__)
#else
#   define dd(...)
#endif


#define MAX_FD 1024


static void *libc_handle = NULL;
static short active_fds[MAX_FD + 1];
static char  polled_fds[MAX_FD + 1];
static char  written_fds[MAX_FD + 1];
static char  weird_fds[MAX_FD + 1];
static char  blacklist_fds[MAX_FD + 1];
static char  snd_timeout_fds[MAX_FD + 1];
static char **matchbufs = NULL;
static size_t matchbuf_len = 0;
static const char *pattern = NULL;
static int verbose = -1;
static int mocking_type = -1;


enum {
    MOCKING_READS = 0x01,
    MOCKING_WRITES = 0x02
};


#   define init_libc_handle() \
        if (libc_handle == NULL) { \
            libc_handle = RTLD_NEXT; \
        }


typedef int (*socket_handle) (int domain, int type, int protocol);

typedef int (*poll_handle) (struct pollfd *ufds, unsigned int nfds,
    int timeout);

typedef ssize_t (*writev_handle) (int fildes, const struct iovec *iov,
    int iovcnt);

typedef int (*close_handle) (int fd);

typedef ssize_t (*send_handle) (int sockfd, const void *buf, size_t len,
    int flags);

typedef ssize_t (*read_handle) (int fd, void *buf, size_t count);

typedef ssize_t (*recv_handle) (int sockfd, void *buf, size_t len,
    int flags);

typedef ssize_t (*recvfrom_handle) (int sockfd, void *buf, size_t len,
    int flags, struct sockaddr *src_addr, socklen_t *addrlen);

#if __linux__
typedef int (*accept4_handle) (int socket, struct sockaddr *address,
    socklen_t *address_len, int flags);

typedef int (*signalfd_handle) (int fd, const sigset_t *mask, int flags);

#if (defined(__GLIBC__) && __GLIBC__ <= 2) && \
    (defined(__GLIBC_MINOR__) && __GLIBC_MINOR__ < 21)
    /* glibc < 2.21 used different signature */
typedef int (*eventfd_handle) (int initval, int flags);

#else
typedef int (*eventfd_handle) (unsigned int initval, int flags);
#endif

#endif


static int get_verbose_level();
static void init_matchbufs();
static int now();
static int get_mocking_type();


#if __linux__
int
accept4(int socket, struct sockaddr *address,
    socklen_t *address_len, int flags)
{
    int fd;

    static accept4_handle       orig_accept4 = NULL;

    init_libc_handle();

    if (orig_accept4 == NULL) {
        orig_accept4 = dlsym(libc_handle, "accept4");
        if (orig_accept4 == NULL) {
            fprintf(stderr, "mockeagain: could not find the underlying accept4: "
                    "%s\n", dlerror());
            exit(1);
        }
    }

    fd = orig_accept4(socket, address, address_len, flags);
    if (fd < 0) {
        return fd;
    }

    if (flags & SOCK_NONBLOCK) {
        if (matchbufs && matchbufs[fd]) {
            free(matchbufs[fd]);
            matchbufs[fd] = NULL;
        }

        active_fds[fd] = 0;
        polled_fds[fd] = 1;
        snd_timeout_fds[fd] = 0;
    }

    return fd;
}


int signalfd(int fd, const sigset_t *mask, int flags)
{
    static signalfd_handle       orig_signalfd = NULL;

    init_libc_handle();

    if (orig_signalfd == NULL) {
        orig_signalfd = dlsym(libc_handle, "signalfd");
        if (orig_signalfd == NULL) {
            fprintf(stderr, "mockeagain: could not find the underlying"
                    " signalfd: %s\n", dlerror());
            exit(1);
        }
    }

    fd = orig_signalfd(fd, mask, flags);
    if (fd < 0) {
        return fd;
    }

    if (get_verbose_level()) {
        fprintf(stderr, "mockeagain: signalfd: blacklist fd %d\n", fd);
    }

    blacklist_fds[fd] = 1;

    return fd;
}


#if (defined(__GLIBC__) && __GLIBC__ <= 2) && \
    (defined(__GLIBC_MINOR__) && __GLIBC_MINOR__ < 21)
    /* glibc < 2.21 used different signature */
int eventfd(int initval, int flags)
#else
int eventfd(unsigned int initval, int flags)
#endif
{
    int                         fd;
    static eventfd_handle       orig_eventfd = NULL;

    init_libc_handle();

    if (orig_eventfd == NULL) {
        orig_eventfd = dlsym(libc_handle, "eventfd");
        if (orig_eventfd == NULL) {
            fprintf(stderr, "mockeagain: could not find the underlying"
                    " eventfd: %s\n", dlerror());
            exit(1);
        }
    }

    fd = orig_eventfd(initval, flags);
    if (fd < 0) {
        return fd;
    }

    if (get_verbose_level()) {
        fprintf(stderr, "mockeagain: eventfd: blacklist fd %d\n", fd);
    }

    blacklist_fds[fd] = 1;

    return fd;
}
#endif


int socket(int domain, int type, int protocol)
{
    int                        fd;
    static socket_handle       orig_socket = NULL;

    dd("calling my socket");

    init_libc_handle();

    if (orig_socket == NULL) {
        orig_socket = dlsym(libc_handle, "socket");
        if (orig_socket == NULL) {
            fprintf(stderr, "mockeagain: could not find the underlying socket: "
                    "%s\n", dlerror());
            exit(1);
        }
    }

    init_matchbufs();

    fd = (*orig_socket)(domain, type, protocol);

    dd("socket with type %d (SOCK_STREAM %d, SOCK_DGRAM %d)", type,
            SOCK_STREAM, SOCK_DGRAM);

    if (fd <= MAX_FD) {
        if (!(type & SOCK_STREAM)) {
            dd("socket: the current fd is weird: %d", fd);
            weird_fds[fd] = 1;

        } else {
            weird_fds[fd] = 0;
        }

#if 1
        if (matchbufs && matchbufs[fd]) {
            free(matchbufs[fd]);
            matchbufs[fd] = NULL;
        }
#endif

        active_fds[fd] = 0;
        polled_fds[fd] = 0;
        snd_timeout_fds[fd] = 0;
    }

    dd("socket returning %d", fd);

    return fd;
}


int
poll(struct pollfd *ufds, nfds_t nfds, int timeout)
{
    static void             *libc_handle;
    int                      retval;
    static poll_handle       orig_poll = NULL;
    struct pollfd           *p;
    int                      i;
    int                      fd = -1;
    int                      begin = 0;
    int                      elapsed = 0;

    dd("calling my poll");

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
            if (fd > MAX_FD || weird_fds[fd]) {
                dd("skipping fd %d", fd);
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

            if (blacklist_fds[fd]) {
                if (get_verbose_level()) {
                    fprintf(stderr, "mockeagain: poll: skip fd %d because it "
                            "is in blacklist\n", fd);
                }

                continue;
            }

            active_fds[fd] = p->revents;
            polled_fds[fd] = 1;

            if (get_verbose_level()) {
                fprintf(stderr, "mockeagain: poll: fd %d polled with events "
                        "%d\n", fd, p->revents);
            }
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
    size_t                   len = 0;

    if ((get_mocking_type() & MOCKING_WRITES)
        && get_verbose_level()
        && fd <= MAX_FD)
    {
        fprintf(stderr, "mockeagain: writev(%d): polled=%d, written=%d, "
                "active=%d\n", fd, (int) polled_fds[fd], (int) written_fds[fd],
                (int) active_fds[fd]);
    }

    if ((get_mocking_type() & MOCKING_WRITES)
        && fd <= MAX_FD
        && polled_fds[fd]
        && written_fds[fd]
        && !(active_fds[fd] & POLLOUT))
    {
        if (get_verbose_level()) {
            fprintf(stderr, "mockeagain: mocking \"writev\" on fd %d to "
                    "signal EAGAIN.\n", fd);
        }

        errno = EAGAIN;
        return -1;
    }

    written_fds[fd] = 1;

    init_libc_handle();

    if (orig_writev == NULL) {
        orig_writev = dlsym(libc_handle, "writev");
        if (orig_writev == NULL) {
            fprintf(stderr, "mockeagain: could not find the underlying writev: "
                    "%s\n", dlerror());
            exit(1);
        }
    }

    if (!(get_mocking_type() & MOCKING_WRITES)) {
        return (*orig_writev)(fd, iov, iovcnt);
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

        if (pattern && new_iov[0].iov_len) {
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
#if (DDEBUG)
        if (polled_fds[fd]) {
            dd("calling the original close on fd %d", fd);
        }
#endif

        if (matchbufs && matchbufs[fd]) {
            free(matchbufs[fd]);
            matchbufs[fd] = NULL;
        }

        active_fds[fd] = 0;
        polled_fds[fd] = 0;
        written_fds[fd] = 0;
        snd_timeout_fds[fd] = 0;
        weird_fds[fd] = 0;
        blacklist_fds[fd] = 0;
    }

    retval = (*orig_close)(fd);

    return retval;
}


ssize_t
send(int fd, const void *buf, size_t len, int flags)
{
    ssize_t                  retval;
    static send_handle       orig_send = NULL;

    dd("calling my send");

    if ((get_mocking_type() & MOCKING_WRITES)
        && fd <= MAX_FD
        && polled_fds[fd]
        && written_fds[fd]
        && !(active_fds[fd] & POLLOUT))
    {
        if (get_verbose_level()) {
            fprintf(stderr, "mockeagain: mocking \"send\" on fd %d to "
                    "signal EAGAIN\n", fd);
        }

        errno = EAGAIN;
        return -1;
    }

    written_fds[fd] = 1;

    init_libc_handle();

    if (orig_send == NULL) {
        orig_send = dlsym(libc_handle, "send");
        if (orig_send == NULL) {
            fprintf(stderr, "mockeagain: could not find the underlying send: "
                    "%s\n", dlerror());
            exit(1);
        }
    }

    if ((get_mocking_type() & MOCKING_WRITES)
        && fd <= MAX_FD
        && polled_fds[fd]
        && len)
    {
        if (get_verbose_level()) {
            fprintf(stderr, "mockeagain: mocking \"send\" on fd %d to emit "
                    "1 byte data only\n", fd);
        }

        if (pattern && len) {
            char          *p;
            size_t         len;
            char           c;

            c = *(char *) buf;

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

        retval = (*orig_send)(fd, buf, 1, flags);
        active_fds[fd] &= ~POLLOUT;

    } else {

        dd("calling the original send on fd %d", fd);

        retval = (*orig_send)(fd, buf, len, flags);
    }

    return retval;
}


ssize_t
read(int fd, void *buf, size_t len)
{
    ssize_t                  retval;
    static read_handle       orig_read = NULL;

    dd("calling my read");

    if ((get_mocking_type() & MOCKING_READS)
        && fd <= MAX_FD
        && polled_fds[fd]
        && !(active_fds[fd] & POLLIN))
    {
        if (get_verbose_level()) {
            fprintf(stderr, "mockeagain: mocking \"read\" on fd %d to "
                    "signal EAGAIN\n", fd);
        }

        errno = EAGAIN;
        return -1;
    }

    init_libc_handle();

    if (orig_read == NULL) {
        orig_read = dlsym(libc_handle, "read");
        if (orig_read == NULL) {
            fprintf(stderr, "mockeagain: could not find the underlying read: "
                    "%s\n", dlerror());
            exit(1);
        }
    }

    if ((get_mocking_type() & MOCKING_READS)
        && fd <= MAX_FD
        && polled_fds[fd]
        && len)
    {
        if (get_verbose_level()) {
            fprintf(stderr, "mockeagain: mocking \"read\" on fd %d to read "
                    "1 byte only\n", fd);
        }

        dd("calling the original read on fd %d", fd);

        retval = (*orig_read)(fd, buf, 1);
        active_fds[fd] &= ~POLLIN;

    } else {
        retval = (*orig_read)(fd, buf, len);
    }

    return retval;
}


ssize_t
recv(int fd, void *buf, size_t len, int flags)
{
    ssize_t                  retval;
    static recv_handle       orig_recv = NULL;

    dd("calling my recv");

    if ((get_mocking_type() & MOCKING_READS)
        && fd <= MAX_FD
        && polled_fds[fd]
        && !(active_fds[fd] & POLLIN))
    {
        if (get_verbose_level()) {
            fprintf(stderr, "mockeagain: mocking \"recv\" on fd %d to "
                    "signal EAGAIN\n", fd);
        }

        errno = EAGAIN;
        return -1;
    }

    init_libc_handle();

    if (orig_recv == NULL) {
        orig_recv = dlsym(libc_handle, "recv");
        if (orig_recv == NULL) {
            fprintf(stderr, "mockeagain: could not find the underlying recv: "
                    "%s\n", dlerror());
            exit(1);
        }
    }

    if ((get_mocking_type() & MOCKING_READS)
        && fd <= MAX_FD
        && polled_fds[fd]
        && len)
    {
        if (get_verbose_level()) {
            fprintf(stderr, "mockeagain: mocking \"recv\" on fd %d to read "
                    "1 byte only\n", fd);
        }

        dd("calling the original recv on fd %d", fd);

        retval = (*orig_recv)(fd, buf, 1, flags);
        active_fds[fd] &= ~POLLIN;

    } else {
        retval = (*orig_recv)(fd, buf, len, flags);
    }

    return retval;
}


ssize_t
recvfrom(int fd, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen)
{
    ssize_t                  retval;
    static recvfrom_handle   orig_recvfrom = NULL;

    dd("calling my recvfrom");

    if ((get_mocking_type() & MOCKING_READS)
        && fd <= MAX_FD
        && polled_fds[fd]
        && !(active_fds[fd] & POLLIN))
    {
        if (get_verbose_level()) {
            fprintf(stderr, "mockeagain: mocking \"recvfrom\" on fd %d to "
                    "signal EAGAIN\n", fd);
        }

        errno = EAGAIN;
        return -1;
    }

    init_libc_handle();

    if (orig_recvfrom == NULL) {
        orig_recvfrom = dlsym(libc_handle, "recvfrom");
        if (orig_recvfrom == NULL) {
            fprintf(stderr, "mockeagain: could not find the underlying recvfrom: "
                    "%s\n", dlerror());
            exit(1);
        }
    }

    if ((get_mocking_type() & MOCKING_READS)
        && fd <= MAX_FD
        && polled_fds[fd]
        && len)
    {
        if (get_verbose_level()) {
            fprintf(stderr, "mockeagain: mocking \"recvfrom\" on fd %d to read "
                    "1 byte only\n", fd);
        }

        dd("calling the original recvfrom on fd %d", fd);

        retval = (*orig_recvfrom)(fd, buf, 1, flags, src_addr, addrlen);
        active_fds[fd] &= ~POLLIN;

    } else {
        retval = (*orig_recvfrom)(fd, buf, len, flags, src_addr, addrlen);
    }

    return retval;
}


static int
get_mocking_type() {
    const char          *p;

#if 1
    if (mocking_type >= 0) {
        return mocking_type;
    }
#endif

    mocking_type = 0;

    p = getenv("MOCKEAGAIN");
    if (p == NULL || *p == '\0') {
        dd("MOCKEAGAIN env empty");
        /* mocking_type = MOCKING_WRITES; */
        return mocking_type;
    }

    while (*p) {
        if (*p == 'r' || *p == 'R') {
            mocking_type |= MOCKING_READS;

        } else if (*p == 'w' || *p == 'W') {
            mocking_type |= MOCKING_WRITES;
        }

        p++;
    }

    if (mocking_type == 0) {
        mocking_type = MOCKING_WRITES;
    }

    dd("mocking_type %d", mocking_type);

    return mocking_type;
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
        dd("MOCKEAGAIN_VERBOSE env empty");
        verbose = 0;
        return verbose;
    }

    if (*p >= '0' && *p <= '9') {
        dd("MOCKEAGAIN_VERBOSE env value: %s", p);
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
        matchbuf_len = 0;
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

