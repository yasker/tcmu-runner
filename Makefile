CFLAGS=-Wall -g -I /usr/include/libnl3
LDLIBS=-lnl-3 -lnl-genl-3 -ldl -lpthread

OBJECTS=main.o api.o

all: tcmu-runner handler_dummy.so

tcmu-runner: $(OBJECTS)
	$(CC) $(CFLAGS) $(OBJECTS) -fPIC -o tcmu-runner $(LDLIBS) -Wl,-E

handler_dummy.so: dummy.c
	$(CC) -shared $(CFLAGS) -fPIC dummy.c -o handler_dummy.so

.PHONY: clean
clean:
	rm -f *~ *.o tcmu-runner *.so
