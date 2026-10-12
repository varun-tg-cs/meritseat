CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic

.PHONY: all clean test
all: admission

admission: src/main.c
	$(CC) $(CFLAGS) src/main.c -o admission

test: admission
	python3 tests/integration.py

clean:
	rm -f admission
