.PHONY: all clean

all: mockeagain.so

%.so: %.c
	$(CC) -g -Wall -Werror -fPIC -shared $< -o $@ -ldl

clean:
	rm -rf *.so *.o *.lo

