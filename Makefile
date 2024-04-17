CFLAGS = -Wall -g
CFLAGS += -fsanitize=address
CC:=clang

toyserver: main.o toyroutine.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

*.o: *.c
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f *.o toyserver
