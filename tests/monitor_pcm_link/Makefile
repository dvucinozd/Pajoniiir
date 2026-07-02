CC ?= gcc
CFLAGS += -std=c99 -Wall -Wextra -Wpedantic -Werror=implicit-function-declaration
CFLAGS += -I../../firmware/main-deck-p4/components/monitor_pcm_link/include
CFLAGS += -I../control_link_protocol/stubs

SRCS = test_monitor_pcm_link.c \
       ../../firmware/main-deck-p4/components/monitor_pcm_link/monitor_pcm_link.c

TARGET = test_monitor_pcm_link

.PHONY: all test clean

all: test

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $^ -o $@

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) test_monitor_pcm_link.exe
