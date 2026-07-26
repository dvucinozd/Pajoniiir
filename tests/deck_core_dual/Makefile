CC      = gcc
CFLAGS  = -Wall -Wextra -Wpedantic -Werror=implicit-function-declaration -std=c99 \
           -Wno-unused-variable -Wno-unused-parameter \
           -DDECK_CORE_PC_TEST \
           -Istubs \
           -I../../firmware/main-deck-p4/components/audio_engine/include \
           -I../../firmware/main-deck-p4/components/beat_jump/include \
           -I../../firmware/main-deck-p4/components/deck_core/include \
           -I../../firmware/main-deck-p4/components/control_link/include \
           -I../../firmware/main-deck-p4/components/hot_cue_store/include \
           -I../../firmware/main-deck-p4/components/library/include

SRCS    = test_deck_core_dual.c \
          control_link_stub.c \
          hot_cue_store_stub.c \
          ../../firmware/main-deck-p4/components/beat_jump/beat_jump.c \
          ../../firmware/main-deck-p4/components/control_link/flx4_led_snapshot.c \
          ../../firmware/main-deck-p4/components/deck_core/deck_core.c

TARGET  = test_deck_core_dual

.PHONY: all test clean

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) test_deck_core_dual.exe
