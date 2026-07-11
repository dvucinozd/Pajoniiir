#include "media_io_gate.h"
#include <assert.h>
#include <stdio.h>

static void test_gate_blocks_second_reader(void)
{
    assert(media_io_gate_init() == ESP_OK);
    assert(!media_io_gate_is_available());
    media_io_gate_set_available(true);
    assert(media_io_gate_is_available());
    assert(media_io_gate_try_begin(0));
    assert(!media_io_gate_try_begin(0));
    media_io_gate_end();
    media_io_gate_set_available(false);
    assert(!media_io_gate_is_available());
    assert(media_io_gate_try_begin(0));
    media_io_gate_end();
}

int main(void)
{
    test_gate_blocks_second_reader();
    puts("media_io_gate tests passed");
    return 0;
}
