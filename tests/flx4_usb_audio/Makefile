CC ?= gcc
CFLAGS += -std=c99 -Wall -Wextra -Wpedantic -Werror
CFLAGS += -I../../firmware/control-board-s3/components/flx4_usb_audio/include

SRCS = test_flx4_uac_descriptors.c \
       ../../firmware/control-board-s3/components/flx4_usb_audio/flx4_uac_descriptors.c

TARGET = test_flx4_uac_descriptors

.PHONY: all test clean

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $^ -o $@

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) test_flx4_uac_descriptors.exe
