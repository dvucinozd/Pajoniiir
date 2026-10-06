CC      = gcc
CFLAGS  = -Wall -Wextra -Wpedantic -Werror=implicit-function-declaration -std=c99
INCS    = -I../../firmware/p4-core/components/usb_storage/include
SRCS    = test_usb_media_partition.c \
          ../../firmware/p4-core/components/usb_storage/usb_media_partition.c
TARGET  = test_usb_media_partition

.PHONY: all test clean

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(INCS) -o $(TARGET) $(SRCS)

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) test_usb_media_partition.exe
