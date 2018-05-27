#include "test_case.h"

int run_test(int fd) {
    int n;
    const char  *buf = "test";
    const int    len = sizeof("test") - 1;
    struct pollfd pfd;

#if __linux__
    int epollfd;
    struct epoll_event eev = {EPOLLOUT, {(void *) buf}};
    struct epoll_event events[10];
    epollfd = epoll_create(1);
    assert(epollfd >= 0);
#endif

    pfd.fd = fd;
    pfd.events = POLLOUT;

    assert(!set_mocking(MOCKING_WRITES));
    /* testtest */
    assert(!set_write_timeout_pattern("tte"));

    assert(poll(&pfd, 1, -1) == 1);

    /* t */
    n = send(fd, buf, len, 0);
    assert(n == 1);

    assert(poll(&pfd, 1, -1) == 1);

    /* e */
    n = send(fd, buf + 1, len, 0);
    assert(n == 1);

    assert(poll(&pfd, 1, -1) == 1);

    /* s */
    n = send(fd, buf + 2, len, 0);
    assert(n == 1);

    assert(poll(&pfd, 1, -1) == 1);

    /* t */
    n = send(fd, buf + 3, len, 0);
    assert(n == 1);

    assert(poll(&pfd, 1, -1) == 1);

    /* t */
    n = send(fd, buf + 3, len, 0);
    assert(n == 1);

    assert(poll(&pfd, 1, -1) == 1);

    /* e */
    n = send(fd, buf + 1, len, 0);
    assert(n == 1);

    pfd.events = POLLIN;
    /* wait for data to come back */
    assert(poll(&pfd, 1, -1) == 1);
    pfd.events = POLLOUT;

    /* times out */
    assert(poll(&pfd, 1, 100) == 0);

#if __linux__
    assert(!epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &eev));
    /* times out */
    assert(epoll_wait(epollfd, events,
                      sizeof(events) / sizeof(events[0]), 100) == 0);

    close(epollfd);
#endif

    n = send(fd, buf + 1, len, 0);
    assert(n == -1);
    assert(errno == EAGAIN);

    return EXIT_SUCCESS;
}
