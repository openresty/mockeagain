.PHONY: all

all: mockeagain.so

%.so: %.c
	$(CC) -Wall -Werror -fPIC -shared $< -o $@ -ldl

