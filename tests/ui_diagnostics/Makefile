# Makefile for UI diagnostics flag tests

CC      = gcc
CFLAGS  = -Wall -Wextra -Wpedantic -std=c99 \
           -I../../firmware/main-deck-p4/components/ui/include

TARGET_DEFAULT = test_ui_diagnostics_default
TARGET_ENABLED = test_ui_diagnostics_enabled

.PHONY: all test clean

all: $(TARGET_DEFAULT) $(TARGET_ENABLED)

$(TARGET_DEFAULT): test_ui_diagnostics_default.c
	$(CC) $(CFLAGS) -o $@ $^

$(TARGET_ENABLED): test_ui_diagnostics_enabled.c
	$(CC) $(CFLAGS) -DUI_DIAGNOSTICS_ENABLED=1 -o $@ $^

test: all
	./$(TARGET_DEFAULT)
	./$(TARGET_ENABLED)

clean:
	rm -f $(TARGET_DEFAULT) $(TARGET_ENABLED)
