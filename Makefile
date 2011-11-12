.PHONY: all

all: slowrites.so

%.so: %.c
	$(CC) -Wall -Werror -fPIC -shared $< -o $@ -ldl

