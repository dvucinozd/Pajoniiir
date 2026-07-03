CC      = gcc
CFLAGS  = -Wall -Wextra -Wpedantic -Werror=implicit-function-declaration -std=c99 \
           -I../../firmware/main-deck-p4/components/audio_engine/include

SRCS    = test_audio_format.c \
          ../../firmware/main-deck-p4/components/audio_engine/audio_format.c

TARGET  = test_audio_format

.PHONY: all test clean

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) test_audio_format.exe
