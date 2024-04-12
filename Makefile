CFLAGS = -Wall -g

toyserver: main.o toyroutine.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

*.o: *.c
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f *.o toyserver