CC = gcc
TARGET = p2p_chat
SRC = src/main.c src/sup.c lib/util.c lib/parse/parse.c lib/net/network.c lib/crypto/crypto.c
CFLAGS = -Wall -Wextra -pedantic -std=c99 -O2

all: $(TARGET)

$(TARGET): $(SRC)
	mkdir -p build
	$(CC) $(SRC) -o build/$(TARGET) $(CFLAGS)

clean:
	rm -rf build
