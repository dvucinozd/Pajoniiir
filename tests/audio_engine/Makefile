# Makefile for audio_engine PC tests
#
# Usage:
#   make              — build test binary
#   make test         — run synthetic tests (no MP3 file needed)
#   make test MP3=<path>         — run all tests including MP3 decode
#   make test MP3=<path> MS=5000 — decode first 5 s of MP3 to out.wav
#   make clean        — remove build artifacts
#
# Output:
#   ./test_audio_engine        — test binary
#   out.wav                    — WAV decoded from full track (test 4)
#   out_from5s.wav             — WAV decoded from 5 s seek point (test 4)

# -std=c11: audio_engine.h (PC test) and the shared esp_err.h stub both
# typedef esp_err_t; identical typedef redefinition is only legal from C11 on.
CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11 \
           -DAUDIO_ENGINE_PC_TEST \
           -DANLZ_STANDALONE_TEST \
           -I../../firmware/main-deck-p4/components/audio_engine \
           -I../../firmware/main-deck-p4/components/audio_engine/include \
           -I../../firmware/main-deck-p4/components/library/include \
           -I../../firmware/main-deck-p4/components/monitor_pcm_link/include \
           -I../control_link_protocol/stubs

AUDIO_SRC = ../../firmware/main-deck-p4/components/audio_engine/audio_engine.c \
            ../../firmware/main-deck-p4/components/audio_engine/audio_delay_fx.c \
            ../../firmware/main-deck-p4/components/audio_engine/audio_eq.c \
            ../../firmware/main-deck-p4/components/audio_engine/audio_filter.c \
            ../../firmware/main-deck-p4/components/audio_engine/audio_pad_fx.c \
            ../../firmware/main-deck-p4/components/audio_engine/audio_mixer.c \
            ../../firmware/main-deck-p4/components/audio_engine/audio_pcm_ring.c \
            ../../firmware/main-deck-p4/components/audio_engine/audio_resampler.c \
            ../../firmware/main-deck-p4/components/audio_engine/audio_output_mixer.c \
            ../../firmware/main-deck-p4/components/audio_engine/audio_fw_preload.c \
            ../../firmware/main-deck-p4/components/audio_engine/audio_fw_runtime.c \
            ../../firmware/main-deck-p4/components/audio_engine/audio_fw_task_context.c \
            ../../firmware/main-deck-p4/components/audio_engine/audio_fw_task_plan.c \
            ../../firmware/main-deck-p4/components/audio_engine/audio_smart_cfx.c \
            ../../firmware/main-deck-p4/components/monitor_pcm_link/monitor_pcm_link.c

SRCS    = test_audio_engine.c \
          $(AUDIO_SRC)

TARGET  = test_audio_engine

MP3     ?=
MS      ?= 0

.PHONY: all test clean

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^ -lm

test: $(TARGET)
ifeq ($(MP3),)
	./$(TARGET)
else
	./$(TARGET) "$(MP3)" $(MS)
endif

clean:
	rm -f $(TARGET) out.wav out_from5s.wav
