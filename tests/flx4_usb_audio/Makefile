CC ?= gcc
CFLAGS += -std=c99 -Wall -Wextra -Wpedantic -Werror
CFLAGS += -I../../firmware/control-board-s3/components/flx4_usb_audio/include

DESCRIPTOR_SRCS = test_flx4_uac_descriptors.c \
                  ../../firmware/control-board-s3/components/flx4_usb_audio/flx4_uac_descriptors.c

PACKETIZER_SRCS = test_flx4_uac_packetizer.c \
                  ../../firmware/control-board-s3/components/flx4_usb_audio/flx4_uac_packetizer.c

TARGETS = test_flx4_uac_descriptors test_flx4_uac_packetizer

.PHONY: all test clean

all: test

test_flx4_uac_descriptors: $(DESCRIPTOR_SRCS)
	$(CC) $(CFLAGS) $^ -o $@

test_flx4_uac_packetizer: $(PACKETIZER_SRCS)
	$(CC) $(CFLAGS) $^ -o $@

test: $(TARGETS)
	./test_flx4_uac_descriptors
	./test_flx4_uac_packetizer

clean:
	rm -f $(TARGETS) test_flx4_uac_descriptors.exe test_flx4_uac_packetizer.exe
