#pragma once

#include <stdint.h>

/* Fixed-size, RAM-only measurements. No logging, allocation or I/O from the
 * refresh ISR or LVGL frame path. Durations are microseconds. */
typedef enum {
    UI_TIMING_WAKE,
    UI_TIMING_FRAME_INTERVAL,
    UI_TIMING_OVERVIEW_BEGIN,
    UI_TIMING_CALLBACK,
    UI_TIMING_HANDLER,
    UI_TIMING_D1_CACHE,
    UI_TIMING_D1_BLIT,
    UI_TIMING_D1_FINISH,
    UI_TIMING_D2_CACHE,
    UI_TIMING_D2_BLIT,
    UI_TIMING_D2_FINISH,
    UI_TIMING_COUNT
} ui_scanout_timing_metric_t;

typedef struct {
    uint32_t count;
    uint32_t last_us;
    uint32_t max_us;
    uint64_t total_us;
    uint32_t histogram[7]; /* <=1,2,4,8,12,20 ms; >20 ms */
} ui_scanout_timing_stat_t;

typedef struct {
    uint32_t refresh_count;
    uint32_t coalesced_refreshes;
    uint32_t zoom_beats;
    ui_scanout_timing_stat_t metric[UI_TIMING_COUNT];
} ui_scanout_timing_snapshot_t;

void ui_scanout_timing_refresh_isr(int64_t now_us);
void ui_scanout_timing_frame_begin(int64_t now_us);
void ui_scanout_timing_overview_begin(uint32_t zoom_beats, int64_t now_us);
void ui_scanout_timing_record(ui_scanout_timing_metric_t metric, uint32_t us);
void ui_scanout_timing_wave_complete(uint8_t deck_index, uint32_t cache_us,
                                     uint32_t blit_us, int64_t now_us);
void ui_scanout_timing_snapshot(ui_scanout_timing_snapshot_t *out);
