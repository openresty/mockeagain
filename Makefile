.PHONY: all clean
CC=gcc

all: mockeagain.so

%.so: %.c
	$(CC) -g -Wall -Werror -fPIC -shared $< -o $@ -ldl || \
	$(CC) -L/usr/local/lib -I/usr/local/include -g -Wall -Werror -fPIC -shared $< -o $@ -lexecinfo || \
	$(CC) -g -Wall -Werror -fPIC -shared $< -o $@

clean:
	rm -rf *.so *.o *.lo

