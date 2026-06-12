# Makefile for UI overlay coordinate mapping tests

CC      = gcc
CFLAGS  = -Wall -Wextra -Wpedantic -std=c99 \
           -I../../firmware/main-deck-p4/components/ui/include

SRCS    = test_ui_overlay_map.c \
          ../../firmware/main-deck-p4/components/ui/ui_overlay_map.c

TARGET  = test_ui_overlay_map

.PHONY: all test clean

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
