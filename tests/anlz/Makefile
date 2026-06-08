# Makefile for ANLZ parser PC tests
#
# Usage:
#   make          — build test binary
#   make test     — build and run unit tests (synthetic files)
#   make clean    — remove build artifacts
#
# Run with a real Rekordbox ANLZ file:
#   ./test_anlz /path/to/ANLZ0000.DAT /path/to/ANLZ0000.EXT

CC      = gcc
CFLAGS  = -Wall -Wextra -Wpedantic \
           -DANLZ_STANDALONE_TEST \
           -I../../firmware/main-deck-p4/components/library/include

SRCS    = test_anlz.c \
          ../../firmware/main-deck-p4/components/library/rekordbox_anlz.c

TARGET  = test_anlz

.PHONY: all test clean

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) test_synth.dat test_synth.ext
