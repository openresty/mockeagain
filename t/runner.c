#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include "test_case.h"

int main(int argc, char *argv[])
{
    struct addrinfo  hints;
    struct addrinfo *result;
    int              fd, s;

    if (argc != 3) {
        fprintf(stderr, "expect two cmdline arguments\n");
        exit(EXIT_FAILURE);
    }

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC; // IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = 0;
    hints.ai_protocol = IPPROTO_TCP;

    s = getaddrinfo(argv[1], argv[2], &hints, &result);
    if (s) {
        fprintf(stderr, "getaddrinfo failed: %s\n", gai_strerror(s));
        exit(EXIT_FAILURE);
    }

    fd = socket(result->ai_family, result->ai_socktype,
                result->ai_protocol);
    if (fd == -1) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    if (connect(fd, result->ai_addr, result->ai_addrlen) == -1) {
        /* note that since we always connect to localhost
        EINPROGRESS is not expected to be returned */
        perror("connect failed");
        exit(EXIT_FAILURE);
    }

    /* make it non-blocking after connect to save some work */
    if (fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK) == -1) {
          perror("fcntl failed");
    }

    freeaddrinfo(result);

    s = run_test(fd);

    /* we are terminating anyway, don't bother with errors */
    shutdown(fd, SHUT_WR);
    close(fd);

    exit(s);
}
