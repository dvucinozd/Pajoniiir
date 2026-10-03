#include <assert.h>
#include <stdio.h>
#include "audio_pvbr_validation.h"

int main(void)
{
    uint32_t table[400] = {0};
    assert(!audio_pvbr_is_valid(NULL, 400, 0));
    assert(!audio_pvbr_is_valid(table, 0, 0));
    assert(!audio_pvbr_is_valid(table, 1, 0));
    assert(!audio_pvbr_is_valid(table, 400, 0));
    for (unsigned i = 0; i < 400; ++i) table[i] = i * 100;
    assert(audio_pvbr_is_valid(table, 400, 0));
    assert(audio_pvbr_is_valid(table, 400, 40000));
    assert(!audio_pvbr_is_valid(table, 400, 39900));
    assert(!audio_pvbr_is_valid(table, 400, 20000));
    table[123] = table[122];
    assert(audio_pvbr_is_valid(table, 400, 40000));
    table[123] = table[122] - 1;
    assert(!audio_pvbr_is_valid(table, 400, 0));
    table[123] = 12300;
    table[399] = 0; /* Partial ANLZ import must not seek backwards at the tail. */
    assert(!audio_pvbr_is_valid(table, 400, 0));
    table[399] = UINT32_MAX;
    assert(!audio_pvbr_is_valid(table, 400, UINT32_MAX));
    for (unsigned i = 0; i < 400; ++i) table[i] = 100;
    assert(!audio_pvbr_is_valid(table, 400, 40000));
    puts("audio_pvbr_validation tests passed");
    return 0;
}
