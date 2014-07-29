#CC=gcc
CFLAGS=-Wall -g -I /usr/include/libnl3
LDFLAGS=-lnl-3 -lnl-genl-3 -ldl

OBJECTS=main.o

tcmu-runner: $(OBJECTS)
	$(CC) $(CFLAGS) $(OBJECTS) -o tcmu-runner $(LDFLAGS)

all: tcmu-runner

.PHONY: clean
clean:
	rm -f *~ *.o tcmu-runner
