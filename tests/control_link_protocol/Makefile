CC      = gcc
CFLAGS  = -Wall -Wextra -Wpedantic -std=c99 \
           -Istubs \
           -I../../firmware/control-board-s3/components/panel_io/include \
           -I../../firmware/control-board-s3/components/control_link/include \
           -I../../firmware/main-deck-p4/components/control_link/include

SRCS    = test_control_link_protocol.c s3_constants.c p4_constants.c

TARGET  = test_control_link_protocol

.PHONY: all test clean

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) test_control_link_protocol.exe
