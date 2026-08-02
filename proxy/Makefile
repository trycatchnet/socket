CC=gcc
CFLAGS= -Wall -g
LDFLAGS = -lpthread

TARGET = web_proxy
OBJS = main.o parse.o

all: $(TARGET)
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TARGET) $(OBJS)
