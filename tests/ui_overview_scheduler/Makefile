# Makefile for Overview redraw scheduler helper tests

CC      = gcc
CFLAGS  = -Wall -Wextra -Wpedantic -std=c99 \
           -I../../firmware/p4-core/components/ui/include

SRCS    = test_ui_overview_scheduler.c \
          ../../firmware/p4-core/components/ui/ui_overview_scheduler.c

TARGET  = test_ui_overview_scheduler

.PHONY: all test clean

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
