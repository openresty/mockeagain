#include "test_case.h"

int run_test(int fd) {
    int n;
    const char  *buf = "test";
    const int    len = sizeof("test") - 1;
    char         rcvbuf[len];
    struct pollfd pfd;

    pfd.fd = fd;
    pfd.events = POLLIN;

    n = send(fd, buf, len, 0);
    assert(n == len);

    n = send(fd, buf, len, 0);
    assert(n == len);

    n = send(fd, buf, len, 0);
    assert(n == len);

    /* wait until data are sent back from server */
    assert(poll(&pfd, 1, -1) == 1);

    n = read(fd, rcvbuf, len);
    assert(n == 4);

    n = read(fd, rcvbuf, len);
    assert(n == 4);

    return EXIT_SUCCESS;
}
