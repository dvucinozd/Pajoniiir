# Makefile for active deck LED view helper tests

CC      = gcc
CFLAGS  = -Wall -Wextra -Wpedantic -std=c99 \
           -I../../firmware/main-deck-p4/components/ui/include

SRCS    = test_ui_active_deck_leds.c \
          ../../firmware/main-deck-p4/components/ui/ui_active_deck_leds.c

TARGET  = test_ui_active_deck_leds

.PHONY: all test clean

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
