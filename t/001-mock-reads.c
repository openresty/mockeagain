#include "test_case.h"

int run_test(int fd) {
    int n, i;
    const char  *buf = "test";
    const int    len = sizeof("test") - 1;
    char         rcvbuf[len];
    struct pollfd pfd;

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

    return EXIT_SUCCESS;
}
