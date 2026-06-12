# Makefile for UI position interpolation tests

CC      = gcc
CFLAGS  = -Wall -Wextra -Wpedantic -std=c99 \
           -I../../firmware/main-deck-p4/components/ui/include

SRCS    = test_ui_position_interpolator.c \
          ../../firmware/main-deck-p4/components/ui/ui_position_interpolator.c

TARGET  = test_ui_position_interpolator

.PHONY: all test clean

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
