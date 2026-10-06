CC      = gcc
CFLAGS  = -Wall -Wextra -Wpedantic -std=c99 \
          -I../../firmware/p4-core/components/ui/include \
          -DUI_PERFORMANCE_TABS_HOST_TEST
SRCS    = test_ui_performance_tabs.c \
          ../../firmware/p4-core/components/ui/ui_performance_tabs.c
TARGET  = test_ui_performance_tabs

.PHONY: all test clean

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $(SRCS)

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
