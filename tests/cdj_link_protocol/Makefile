CC      = gcc
CFLAGS  = -Wall -Wextra -Wpedantic -std=c99 \
           -DCDJ_LINK_PROTOCOL_STANDALONE_TEST \
           -I../../firmware/main-deck-p4/components/cdj_link_protocol/include

SRCS    = test_cdj_link_protocol.c \
          ../../firmware/main-deck-p4/components/cdj_link_protocol/cdj_link_protocol.c

TARGET  = test_cdj_link_protocol

.PHONY: all test clean

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) test_cdj_link_protocol.exe
