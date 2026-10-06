#include <assert.h>
#include <stdio.h>
#include "ui_scanout_timing.h"

int main(void)
{
    ui_scanout_timing_snapshot_t snapshot;
    ui_scanout_timing_refresh_isr(10000);
    ui_scanout_timing_frame_begin(10120);
    ui_scanout_timing_overview_begin(8, 10300);
    ui_scanout_timing_wave_complete(0, 100, 500, 11100);
    /* A newer interrupt must not move the origin of a frame still in flight. */
    ui_scanout_timing_refresh_isr(30000);
    ui_scanout_timing_wave_complete(1, 200, 800, 31000);
    ui_scanout_timing_refresh_isr(50000);
    ui_scanout_timing_frame_begin(50120);
    ui_scanout_timing_snapshot(&snapshot);
    assert(snapshot.refresh_count == 3);
    assert(snapshot.coalesced_refreshes == 1);
    assert(snapshot.zoom_beats == 8);
    assert(snapshot.metric[UI_TIMING_WAKE].last_us == 120);
    assert(snapshot.metric[UI_TIMING_FRAME_INTERVAL].last_us == 40000);
    assert(snapshot.metric[UI_TIMING_OVERVIEW_BEGIN].last_us == 300);
    assert(snapshot.metric[UI_TIMING_D1_FINISH].last_us == 1100);
    assert(snapshot.metric[UI_TIMING_D2_FINISH].last_us == 21000);
    const uint32_t values[] = {1000,1001,2000,2001,4000,4001,8000,
                               8001,12000,12001,20000,20001};
    uint64_t total = 0;
    for (unsigned i = 0; i < sizeof values / sizeof values[0]; ++i) {
        ui_scanout_timing_record(UI_TIMING_CALLBACK, values[i]);
        total += values[i];
    }
    ui_scanout_timing_record((ui_scanout_timing_metric_t)-1, 999);
    ui_scanout_timing_wave_complete(2, 999, 999, 99999);
    ui_scanout_timing_snapshot(NULL);
    ui_scanout_timing_snapshot(&snapshot);
    assert(snapshot.metric[UI_TIMING_CALLBACK].count == 12);
    assert(snapshot.metric[UI_TIMING_CALLBACK].total_us == total);
    assert(snapshot.metric[UI_TIMING_CALLBACK].max_us == 20001);
    for (unsigned i = 0; i < 7; ++i) {
        assert(snapshot.metric[UI_TIMING_CALLBACK].histogram[i] ==
               (i == 0 || i == 6 ? 1u : 2u));
    }
    puts("PASS scanout timing: in-flight refresh origin, coalescing, histogram boundaries");
    return 0;
}
