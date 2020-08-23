CFLAGS = -g -pthread -MD

all: host.out client.out

host.out: host.o
	$(CC) $(CFLAGS) -o $@ $<

client.out: client.o
	$(CC) $(CFLAGS) -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

-include *.d

.PHONY: clean

clean:
	rm *.o *.out *.d
