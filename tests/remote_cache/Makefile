CC      = gcc
CFLAGS  = -Wall -Wextra -Wpedantic -std=c99 \
           -I../../firmware/main-deck-p4/components/remote_cache/include

SRCS    = test_remote_cache_retry.c \
          ../../firmware/main-deck-p4/components/remote_cache/remote_cache_retry.c

TARGET  = test_remote_cache_retry

.PHONY: all test clean

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) test_remote_cache_retry.exe
