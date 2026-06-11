# Makefile for Overview waveform grid helper tests

CC      = gcc
CFLAGS  = -Wall -Wextra -Wpedantic -std=c99 \
           -DANLZ_STANDALONE_TEST \
           -I../../firmware/main-deck-p4/components/ui/include \
           -I../../firmware/main-deck-p4/components/library/include

SRCS    = test_ui_overview_grid.c \
          ../../firmware/main-deck-p4/components/ui/ui_overview_grid.c

TARGET  = test_ui_overview_grid

.PHONY: all test clean

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
