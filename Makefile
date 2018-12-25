CC			= gcc
LD			= gcc
CFLAGS		= -I. -std=c99
LDFLAGS		= -lconfig -lpaho-mqtt3a -lcurl -ljson-c -lm
DEPS		= neurio.h
PROG_NAME   = neurio-thingsboard

SRC_DIR     = ./src
BUILD_DIR   = ./build
BIN_DIR     = ./bin

SRC_LIST = $(wildcard $(SRC_DIR)/*.c)
OBJ_LIST = $(BUILD_DIR)/$(notdir $(SRC_LIST:.c=.o))

.PHONY: clean all $(PROG_NAME) compile

all: $(PROG_NAME)

compile:
	$(CC) -c $(CFLAGS) $(SRC_LIST) -o $(OBJ_LIST)

$(PROG_NAME): compile
	$(LD) $(LDFLAGS) $(OBJ_LIST) -o $(BIN_DIR)/$@


clean:
	rm -f $(obj) ../bin/neurio-thingsboard
