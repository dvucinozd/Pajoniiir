# Makefile for UI position interpolation tests

CC      = gcc
CFLAGS  = -Wall -Wextra -Wpedantic -std=c99 \
           -I../../firmware/p4-core/components/ui/include

SRCS    = test_ui_position_interpolator.c \
          ../../firmware/p4-core/components/ui/ui_position_interpolator.c

TARGET  = test_ui_position_interpolator

.PHONY: all test clean

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
