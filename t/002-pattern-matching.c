#include "test_case.h"

int run_test(int fd) {
    int n;
    const char  *buf = "test";
    const int    len = sizeof("test") - 1;
    struct pollfd pfd;

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

    /* times out */
    assert(poll(&pfd, 1, 100) == 0);
    assert(poll(&pfd, 1, 100) == 0);

    n = send(fd, buf + 1, len, 0);
    assert(n == -1);
    assert(errno == EAGAIN);

    return EXIT_SUCCESS;
}
