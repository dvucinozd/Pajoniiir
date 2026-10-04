#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "ui_track_duration.h"

int main(void)
{
    assert(ui_track_duration_select(120000, 7, true, 7, 100) == 100);
    assert(ui_track_duration_select(100, 7, true, 7, 120000) == 120000);
    assert(ui_track_duration_select(100, 7, true, 8, 120000) == 100);
    assert(ui_track_duration_select(100, 0, true, 0, 120000) == 100);
    assert(ui_track_duration_select(100, 7, false, 7, 120000) == 100);
    assert(ui_track_duration_select(100, 7, true, 7, 0) == 100);
    assert(ui_track_duration_select(0, 7, true, 7, 120000) == 120000);
    assert(ui_track_duration_select(UINT32_MAX, UINT32_MAX, true,
                                     UINT32_MAX, 1) == 1);
    puts("ui_track_duration tests passed");
    return 0;
}
