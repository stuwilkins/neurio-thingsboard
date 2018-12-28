BIN_DIR = ../bin
VPATH = $(BIN_DIR)

CC=gcc
CFLAGS += -I. -Wall -std=c99 -O3 
LDFLAGS += -lpaho-mqtt3c -lcurl -ljson-c -lm -lconfig

DEPS = neurio.h

src = $(wildcard *.c)
obj = $(src:.c=.o)

.PHONY: clean

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

neurio-thingsboard: $(obj)
	@mkdir -p $(BIN_DIR)/$(@D)
	$(CC) -o $(BIN_DIR)/$@ $^ $(LDFLAGS)

clean:
	rm -f $(obj) .$(BIN_DIR)/neurio-thingsboard
