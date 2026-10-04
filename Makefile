# NLE - build with any C99 compiler. No dependencies except libm.
CC      ?= cc
CFLAGS  ?= -O2
CFLAGS  += -std=c99 -D_POSIX_C_SOURCE=200809L -Wall -Wextra
LIB      = src/expr.c src/solve.c src/nn.c src/crypto.c src/logic.c src/gfx.c src/view.c

nle: $(LIB) src/main.c src/nle.h
	$(CC) $(CFLAGS) -o $@ $(LIB) src/main.c -lm

test: $(LIB) tests/test.c src/nle.h
	$(CC) $(CFLAGS) -o nle_test $(LIB) tests/test.c -lm && ./nle_test

clean:
	rm -f nle nle_test *.ppm
.PHONY: test clean
