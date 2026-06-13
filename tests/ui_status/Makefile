# Makefile for UI status helper tests

CC      = gcc
CFLAGS  = -Wall -Wextra -Wpedantic -std=c99 \
           -I../deck_core_dual/stubs \
           -I../../firmware/main-deck-p4/components/ui/include \
           -I../../firmware/main-deck-p4/components/deck_core/include \
           -I../../firmware/main-deck-p4/components/control_link/include \
           -I../../firmware/main-deck-p4/components/library/include

SRCS    = test_ui_status.c \
          ../../firmware/main-deck-p4/components/ui/ui_status.c

TARGET  = test_ui_status

.PHONY: all test clean

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -DUI_STATUS_HOST_TEST -DWIN32 -o $@ $^

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
