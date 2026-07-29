# Makefile for UI per-deck ANLZ store tests

CC      = gcc
CFLAGS  = -Wall -Wextra -Wpedantic -std=c99 \
           -DANLZ_STANDALONE_TEST \
           -I../../firmware/main-deck-p4/components/ui/include \
           -I../../firmware/main-deck-p4/components/library/include

SRCS    = test_ui_deck_anlz_store.c \
          ../../firmware/main-deck-p4/components/ui/ui_deck_anlz_store.c \
          ../../firmware/main-deck-p4/components/library/anlz_snapshot.c \
          anlz_free_stub.c

TARGET  = test_ui_deck_anlz_store

.PHONY: all test clean

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
