# Makefile for UI settings helper tests

CC      = gcc
CFLAGS  = -Wall -Wextra -Wpedantic -std=c99 \
           -I../../firmware/p4-core/components/ui/include

SRCS    = test_ui_settings.c \
          ../../firmware/p4-core/components/ui/ui_settings.c

TARGET  = test_ui_settings

.PHONY: all test clean

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -DUI_SETTINGS_HOST_TEST -o $@ $^

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
