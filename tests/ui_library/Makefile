# Makefile for Library UI helper tests

CC      = gcc
CFLAGS  = -Wall -Wextra -Wpedantic -std=c99 \
           -I../../firmware/main-deck-p4/components/ui/include

SRCS    = test_ui_library.c \
          ../../firmware/main-deck-p4/components/ui/ui_library.c

TARGET  = test_ui_library

.PHONY: all test clean

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -DUI_LIBRARY_HOST_TEST -o $@ $^

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
