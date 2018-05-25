#include "test_case.h"
#if __linux__
#include <sys/signalfd.h>
#include <sys/eventfd.h>
#endif

int run_test(int fd) {
    (void) fd;

#if __linux__
    struct pollfd pfd;
    ssize_t n;
    int sfd;
    struct signalfd_siginfo sfd_info;
    sigset_t mask;
    int efd;
    uint64_t efd_events;

    sigemptyset(&mask);
    sigaddset(&mask, SIGPIPE);
    assert(sigprocmask(SIG_BLOCK, &mask, NULL) != -1);

    sfd = signalfd(-1, &mask, SFD_NONBLOCK);
    assert(sfd != -1);

    pfd.fd = sfd;
    pfd.events = POLLIN;
    assert(!set_mocking(MOCKING_READS));

    assert(kill(getpid(), SIGPIPE) != -1);
    assert(poll(&pfd, 1, -1) == 1);

    n = read(sfd, &sfd_info, sizeof(struct signalfd_siginfo));
    assert(n == sizeof(struct signalfd_siginfo));

    close(sfd);

    efd = eventfd(0, EFD_NONBLOCK);
    pfd.fd = efd;
    pfd.events = POLLIN;

    efd_events = 1;
    assert(write(efd, &efd_events, sizeof(uint64_t)) != -1);
    assert(poll(&pfd, 1, -1) == 1);

    n = read(efd, &efd_events, sizeof(uint64_t));
    assert(n == sizeof(uint64_t));

    close(efd);
#endif

    return EXIT_SUCCESS;
}
