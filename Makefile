CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c17
LDFLAGS = -lssl -lcrypto

OBJS = main.o config.o url.o io.o net.o http.o icy.o stream.o client.o

all: sikradio

sikradio: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

%.o: %.c sikradio.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o sikradio

.PHONY: all clean
