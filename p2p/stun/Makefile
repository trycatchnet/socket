CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -Wpedantic -Wshadow -g -O1
SAN     ?= -fsanitize=address,undefined -fno-omit-frame-pointer

SRC     := stun.c net.c
TEST    := tests/runner.c

.PHONY: test client natcheck server clean

test: build/runner
	@./build/runner tests/corpus

natcheck: build/natcheck
	@echo "built: ./build/natcheck"

build/natcheck: $(SRC) natcheck.c stun.h net.h | build
	$(CC) $(CFLAGS) $(SAN) -o $@ $(SRC) natcheck.c

server: build/stunserver
	@echo "built: ./build/stunserver"

build/stunserver: $(SRC) stunserver.c stun.h net.h | build
	$(CC) $(CFLAGS) $(SAN) -o $@ $(SRC) stunserver.c

client: build/stunclient
	@echo "built: ./build/stunclient"

build/stunclient: $(SRC) main.c stun.h net.h | build
	$(CC) $(CFLAGS) $(SAN) -o $@ $(SRC) main.c

build/runner: $(SRC) $(TEST) stun.h | build
	$(CC) $(CFLAGS) $(SAN) -o $@ $(SRC) $(TEST)

build:
	@mkdir -p build

clean:
	rm -rf build
