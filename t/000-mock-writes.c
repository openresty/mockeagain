#include "test_case.h"

int run_test(int fd) {
    int n;
    const char  *buf = "test";
    const int    len = sizeof("test") - 1;
    struct iovec iov = {(void *) buf, len};
    struct pollfd pfd;

#if __linux__
    int epollfd;
    struct epoll_event eev = {EPOLLIN|EPOLLOUT, {(void *) buf}};
    struct epoll_event events[10];
    epollfd = epoll_create(1);
    assert(epollfd >= 0);
#endif

    pfd.fd = fd;
    pfd.events = POLLOUT;

    assert(!set_mocking(MOCKING_WRITES));

    /* haven't called poll(). mocking is not active */
    n = send(fd, buf, len, 0);
    assert(n == len);

    assert(poll(&pfd, 1, -1) == 1);

    /* mocking is active and we haven't wrote yet */
    n = send(fd, buf, len, 0);
    assert(n == 1);

    /* since we have already wrote it's always going to EAGAIN */
    n = send(fd, buf, len, 0);
    assert(n == -1);
    assert(errno == EAGAIN);

    /* same as above */
    n = send(fd, buf, len, 0);
    assert(n == -1);
    assert(errno == EAGAIN);

    assert(poll(&pfd, 1, -1) == 1);

    /* can write one byte now */
    n = send(fd, buf, len, 0);
    assert(n == 1);

    /* EAGAIN now because we haven't polled */
    n = writev(fd, &iov, 1);
    assert(n == -1);
    assert(errno == EAGAIN);

    assert(poll(&pfd, 1, -1) == 1);

    n = writev(fd, &iov, 1);
    assert(n == 1);

    n = writev(fd, &iov, 1);
    assert(n == -1);
    assert(errno == EAGAIN);

#if __linux__
    assert(!epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &eev));
    n = writev(fd, &iov, 1);
    assert(n == -1);
    assert(errno == EAGAIN);

    assert(epoll_wait(epollfd, events,
                      sizeof(events) / sizeof(events[0]), 0) == 1);
    assert(events[0].events & EPOLLOUT);
    assert(events[0].data.ptr == buf);

    /* simulated ET */
    assert(epoll_wait(epollfd, events,
                      sizeof(events) / sizeof(events[0]), 0) == 0);

    n = writev(fd, &iov, 1);
    assert(n == 1);

    n = writev(fd, &iov, 1);
    assert(n == -1);
    assert(errno == EAGAIN);

    assert(epoll_wait(epollfd, events,
                      sizeof(events) / sizeof(events[0]), 0) == 1);
    assert(events[0].events & EPOLLOUT);
    assert(events[0].data.ptr == buf);

    n = writev(fd, &iov, 1);
    assert(n == 1);

    n = writev(fd, &iov, 1);
    assert(n == -1);
    assert(errno == EAGAIN);

    close(epollfd);
#endif

    return EXIT_SUCCESS;
}
