# Makefile for Library UI helper tests

CC      = gcc
CFLAGS  = -Wall -Wextra -Wpedantic -std=c99 \
           -I../../firmware/p4-core/components/ui/include

SRCS    = test_ui_library.c \
          ../../firmware/p4-core/components/ui/ui_library.c

TARGET  = test_ui_library

.PHONY: all test clean

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -DUI_LIBRARY_HOST_TEST -o $@ $^

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
