#include "test_case.h"
#include <sys/uio.h>

int run_test(int fd) {
    int n;
    const char  *buf = "test";
    const int    len = sizeof("test") - 1;
    struct iovec iov = {(void *) buf, len};
    struct pollfd pfd;

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

    return EXIT_SUCCESS;
}
