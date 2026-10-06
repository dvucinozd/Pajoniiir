# Makefile for Overview waveform renderer tests

CC      = gcc
CFLAGS  = -Wall -Wextra -Wpedantic -std=c99 \
           -DANLZ_STANDALONE_TEST \
           -I../../firmware/p4-core/components/ui/include \
           -I../../firmware/p4-core/components/library/include

SRCS    = test_ui_overview_renderer.c \
          ../../firmware/p4-core/components/ui/ui_overview_renderer.c \
          ../../firmware/p4-core/components/ui/ui_overview_grid.c \
          ../../firmware/p4-core/components/ui/ui_waveform_model.c

TARGET  = test_ui_overview_renderer

.PHONY: all test clean

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
