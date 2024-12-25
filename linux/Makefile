CC = aarch64-linux-gnu-gcc
CFLAGS = -Wall -O2 -I/usr/aarch64-linux-gnu/include
LDFLAGS = -L/usr/aarch64-linux-gnu/lib -lmosquitto -lmicrohttpd -ljson-c

SRCS = src/main.c src/aht10.c src/webserver.c src/logger.c
OBJS = $(SRCS:.c=.o)
TARGET = temp_control

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET) 