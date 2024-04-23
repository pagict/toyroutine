CFLAGS = -Wall -g
# CFLAGS += -fsanitize=address
CC:=clang

all: monolithic_server client_threads_server

monolithic_server: main.c toyroutine.o
	$(CC) $(CFLAGS) -DCLIENT_THREAD=0 -o $@ $^ $(LDFLAGS)

client_threads_server: main.c toyroutine.o
	$(CC) $(CFLAGS) -DCLIENT_THREAD=3 -o $@ $^ $(LDFLAGS)

*.o: *.c
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f *.o monolithic_server client_threads_server
