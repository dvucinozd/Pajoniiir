CC      = gcc
CFLAGS  = -Wall -Wextra -Wpedantic -std=c99 \
           -DHOT_CUE_STORE_STANDALONE_TEST \
           -I../../firmware/p4-core/components/hot_cue_store/include \
           -I../../firmware/p4-core/components/media_identity/include

SRCS    = test_hot_cue_store.c \
          ../../firmware/p4-core/components/hot_cue_store/hot_cue_store.c \
          ../../firmware/p4-core/components/media_identity/media_identity.c

TARGET  = test_hot_cue_store

.PHONY: all test clean

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) test_hot_cue_store.exe
