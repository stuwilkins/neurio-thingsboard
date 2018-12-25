CC=gcc
CFLAGS=-I. -std=c99
LDFLAGS=-lconfig -lpaho-mqtt3a -lcurl -ljson-c -lm
DEPS=neurio.h
src = $(wildcard *.c)
obj = $(src:.c=.o)

.PHONY: clean

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

neurio-thingsboard: $(obj)
	@mkdir -p ../bin/$(@D)
	$(CC) -o ../bin/$@ $^ $(LDFLAGS)

clean:
	rm -f $(obj) ../bin/neurio-thingsboard
