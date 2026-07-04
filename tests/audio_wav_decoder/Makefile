CC      = gcc
CFLAGS  = -Wall -Wextra -Wpedantic -Werror=implicit-function-declaration -std=c99 \
           -DAUDIO_DECODER_PC_TEST -DMEDIA_IO_GATE_STANDALONE_TEST \
           -I../../firmware/main-deck-p4/components/audio_engine/include \
           -I../../firmware/main-deck-p4/components/media_io_gate/include

SRCS    = test_audio_wav_decoder.c \
          ../../firmware/main-deck-p4/components/audio_engine/audio_format.c \
          ../../firmware/main-deck-p4/components/audio_engine/audio_decoder.c \
          ../../firmware/main-deck-p4/components/audio_engine/audio_wav_decoder.c \
          ../../firmware/main-deck-p4/components/media_io_gate/media_io_gate.c

TARGET  = test_audio_wav_decoder

.PHONY: all test clean

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) test_audio_wav_decoder.exe test_stereo.wav test_mono.wav test_24bit.wav
