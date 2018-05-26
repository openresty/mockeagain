CC?=gcc
COPTS=-O -g -Wall -Werror
ROOT_DIR:=$(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))
ALL_TESTS=$(shell find t -name "[0-9]*.c")
VALGRIND:=0

.PHONY: all test clean

all: mockeagain.so

%.so: %.c
	$(CC) $(COPTS) -fPIC -shared $< -o $@ -ldl || \
	$(CC) $(COPTS) -fPIC -shared $< -o $@

test: all $(ALL_TESTS)
	for t in $(ALL_TESTS); do \
		$(CC) $(COPTS) -o ./t/runner $$t ./t/runner.c ./t/test_case.c \
		|| exit 1; \
		python ./t/echo_server.py ./t/runner $(VALGRIND) $(ROOT_DIR)/mockeagain.so \
		&& echo "Test case $$t passed" || exit 1; \
	done

clean:
	rm -rf *.so *.o *.lo t/runner

