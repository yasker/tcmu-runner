CFLAGS=-Wall -g -I /usr/include/libnl3
LDLIBS=-lnl-3 -lnl-genl-3 -ldl -lpthread

OBJECTS=main.o

all: tcmu-runner handler_dummy.so

tcmu-runner: $(OBJECTS)
	$(CC) $(CFLAGS) $(OBJECTS) -o tcmu-runner $(LDLIBS)

handler_dummy.so: dummy.c
	$(CC) -shared $(CFLAGS) -fPIC dummy.c -o handler_dummy.so

.PHONY: clean
clean:
	rm -f *~ *.o tcmu-runner *.so
