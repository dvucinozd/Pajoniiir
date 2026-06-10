CC      = gcc
CFLAGS  = -Wall -Wextra -Wpedantic -Werror=implicit-function-declaration -std=c99 \
           -I../../firmware/main-deck-p4/components/audio_engine/include

SRCS    = test_audio_fw_runtime.c \
          ../../firmware/main-deck-p4/components/audio_engine/audio_fw_runtime.c

TARGET  = test_audio_fw_runtime

.PHONY: all test clean

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) test_audio_fw_runtime.exe
