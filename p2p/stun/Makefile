CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -Wpedantic -g -O1
# The parser eats bytes from the network. Let the sanitizers watch it.
SAN     ?= -fsanitize=address,undefined -fno-omit-frame-pointer

SRC     := stun.c
TEST    := tests/runner.c

.PHONY: test client clean

test: build/runner
	@./build/runner tests/corpus

client: build/stunclient
	@echo "built: ./build/stunclient"

build/stunclient: $(SRC) main.c stun.h | build
	$(CC) $(CFLAGS) $(SAN) -o $@ $(SRC) main.c

build/runner: $(SRC) $(TEST) stun.h | build
	$(CC) $(CFLAGS) $(SAN) -o $@ $(SRC) $(TEST)

build:
	@mkdir -p build

clean:
	rm -rf build
