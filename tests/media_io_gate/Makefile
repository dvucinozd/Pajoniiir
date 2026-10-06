CC      = gcc
CFLAGS  = -Wall -Wextra -Wpedantic -std=c99 \
           -DMEDIA_IO_GATE_STANDALONE_TEST \
           -I../../firmware/p4-core/components/media_io_gate/include

SRCS    = test_media_io_gate.c \
          ../../firmware/p4-core/components/media_io_gate/media_io_gate.c

TARGET  = test_media_io_gate

.PHONY: all test clean

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) test_media_io_gate.exe
