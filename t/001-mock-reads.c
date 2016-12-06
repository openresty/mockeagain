#include "test_case.h"

int run_test(int fd) {
    int n, i;
    const char  *buf = "test";
    const int    len = sizeof("test") - 1;
    char         rcvbuf[len];
    struct pollfd pfd;

#if __linux__
    int epollfd;
    struct epoll_event eev = {EPOLLIN|EPOLLOUT, {(void *) buf}};
    struct epoll_event events[10];
    epollfd = epoll_create(1);
    assert(epollfd >= 0);
#endif

    pfd.fd = fd;
    pfd.events = POLLIN;

    assert(!set_mocking(MOCKING_READS));

    /* get 4 * 100 = 400 bytes inside receive buf for reading */
    for (i = 0; i < 100; i++) {
        n = send(fd, buf, len, 0);
        assert(n == len);
    }

    /* wait until data are sent back from server */
    assert(poll(&pfd, 1, -1) == 1);

    n = read(fd, rcvbuf, len);
    assert(n == 1);

    n = read(fd, rcvbuf, len);
    assert(n == -1);
    assert(errno == EAGAIN);

    assert(poll(&pfd, 1, -1) == 1);

    /* can read one byte now */
    n = read(fd, rcvbuf, len);
    assert(n == 1);

    /* EAGAIN now because we haven't polled */
    n = recv(fd, rcvbuf, len, 0);
    assert(n == -1);
    assert(errno == EAGAIN);

    n = recvfrom(fd, rcvbuf, len, 0, NULL, NULL);
    assert(n == -1);
    assert(errno == EAGAIN);

    assert(poll(&pfd, 1, -1) == 1);

    n = recv(fd, rcvbuf, len, 0);
    assert(n == 1);

    assert(poll(&pfd, 1, -1) == 1);

    n = recvfrom(fd, rcvbuf, len, 0, NULL, NULL);
    assert(n == 1);

    n = recvfrom(fd, rcvbuf, len, 0, NULL, NULL);
    assert(n == -1);
    assert(errno == EAGAIN);

#if __linux__
    n = recv(fd, rcvbuf, len, 0);
    assert(n == -1);
    assert(errno == EAGAIN);

    assert(!epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &eev));

    n = recv(fd, rcvbuf, len, 0);
    assert(n == -1);
    assert(errno == EAGAIN);

    assert(epoll_wait(epollfd, events,
                      sizeof(events) / sizeof(events[0]), 0) == 1);
    assert(events[0].events & EPOLLIN);
    assert(events[0].data.ptr == buf);

    /* simulated ET */
    assert(epoll_wait(epollfd, events,
                      sizeof(events) / sizeof(events[0]), 0) == 0);

    n = read(fd, rcvbuf, len);
    assert(n == 1);

    n = recvfrom(fd, rcvbuf, len, 0, NULL, NULL);
    assert(n == -1);
    assert(errno == EAGAIN);

    assert(epoll_wait(epollfd, events,
                      sizeof(events) / sizeof(events[0]), 0) == 1);
    assert(events[0].events & EPOLLIN);
    assert(events[0].data.ptr == buf);

    n = read(fd, rcvbuf, len);
    assert(n == 1);

    n = recvfrom(fd, rcvbuf, len, 0, NULL, NULL);
    assert(n == -1);
    assert(errno == EAGAIN);

    close(epollfd);
#endif

    return EXIT_SUCCESS;
}
