CC      = gcc
CFLAGS  = -Wall -Wextra -Wpedantic -std=c99 \
           -I../../firmware/control-board-s3/components/flx4_midi_host/include

SRCS    = test_flx4_midi_host.c \
          ../../firmware/control-board-s3/components/flx4_midi_host/flx4_midi_host.c

TARGET  = test_flx4_midi_host

.PHONY: all test clean

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -DFLX4_MIDI_HOST_PC_TEST -o $@ $^

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) test_flx4_midi_host.exe
