# Makefile for UI controls state helper tests

CC      = gcc
CFLAGS  = -Wall -Wextra -Wpedantic -std=c99 \
           -I../../firmware/main-deck-p4/components/ui/include

SRCS    = test_ui_controls.c \
          ../../firmware/main-deck-p4/components/ui/ui_controls.c \
          ../../firmware/main-deck-p4/components/ui/ui_performance_target.c

TARGET  = test_ui_controls

.PHONY: all test clean

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -DUI_CONTROLS_HOST_TEST -o $@ $^

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
