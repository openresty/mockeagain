#include "test_case.h"

int set_mocking(int types) {
    char   env[3] = {0};
    size_t pos = 0;

    if (types & MOCKING_READS) {
        env[pos++] = 'r';
    }

    if (types & MOCKING_WRITES) {
        env[pos++] = 'w';
    }

    return setenv("MOCKEAGAIN", env, 1);
}

int set_write_timeout_pattern(const char *pattern) {
    if (!strlen(pattern)) {
        return 0;
    }

    return setenv("MOCKEAGAIN_WRITE_TIMEOUT_PATTERN", pattern, 1);
}
