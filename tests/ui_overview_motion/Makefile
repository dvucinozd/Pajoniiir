# Makefile for Overview waveform motion helper tests

CC      = gcc
CFLAGS  = -Wall -Wextra -Wpedantic -std=c99 \
           -DANLZ_STANDALONE_TEST \
           -I../../firmware/p4-core/components/ui/include \
           -I../../firmware/p4-core/components/library/include

SRCS    = test_ui_overview_motion.c \
          ../../firmware/p4-core/components/ui/ui_overview_motion.c

TARGET  = test_ui_overview_motion

.PHONY: all test clean

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
