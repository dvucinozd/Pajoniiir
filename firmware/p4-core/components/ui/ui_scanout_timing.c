#include "ui_scanout_timing.h"

#include "esp_attr.h"
#include "freertos/FreeRTOS.h"

static portMUX_TYPE s_timing_lock = portMUX_INITIALIZER_UNLOCKED;
static ui_scanout_timing_snapshot_t s_timing;
static int64_t s_refresh_us;
static int64_t s_frame_refresh_us;
static int64_t s_previous_frame_us;
static uint32_t s_frame_refresh_count;

static uint32_t elapsed_us(int64_t now_us, int64_t start_us)
{
    if (start_us <= 0 || now_us < start_us) return 0;
    uint64_t us = (uint64_t)(now_us - start_us);
    return us > UINT32_MAX ? UINT32_MAX : (uint32_t)us;
}

static void record_locked(ui_scanout_timing_metric_t metric, uint32_t us)
{
    static const uint32_t limits[] = {1000, 2000, 4000, 8000, 12000, 20000};
    ui_scanout_timing_stat_t *stat = &s_timing.metric[metric];
    stat->count++;
    stat->last_us = us;
    if (us > stat->max_us) stat->max_us = us;
    stat->total_us += us;
    unsigned bucket = 0;
    while (bucket < 6 && us > limits[bucket]) bucket++;
    stat->histogram[bucket]++;
}

void IRAM_ATTR ui_scanout_timing_refresh_isr(int64_t now_us)
{
    portENTER_CRITICAL_ISR(&s_timing_lock);
    s_refresh_us = now_us;
    s_timing.refresh_count++;
    portEXIT_CRITICAL_ISR(&s_timing_lock);
}

void ui_scanout_timing_frame_begin(int64_t now_us)
{
    portENTER_CRITICAL(&s_timing_lock);
    uint32_t gap = s_timing.refresh_count - s_frame_refresh_count;
    if (s_frame_refresh_count && gap > 1) s_timing.coalesced_refreshes += gap - 1;
    s_frame_refresh_count = s_timing.refresh_count;
    s_frame_refresh_us = s_refresh_us;
    record_locked(UI_TIMING_WAKE, elapsed_us(now_us, s_frame_refresh_us));
    if (s_previous_frame_us) {
        record_locked(UI_TIMING_FRAME_INTERVAL, elapsed_us(now_us, s_previous_frame_us));
    }
    s_previous_frame_us = now_us;
    portEXIT_CRITICAL(&s_timing_lock);
}

void ui_scanout_timing_overview_begin(uint32_t zoom_beats, int64_t now_us)
{
    portENTER_CRITICAL(&s_timing_lock);
    s_timing.zoom_beats = zoom_beats;
    record_locked(UI_TIMING_OVERVIEW_BEGIN, elapsed_us(now_us, s_frame_refresh_us));
    portEXIT_CRITICAL(&s_timing_lock);
}

void ui_scanout_timing_record(ui_scanout_timing_metric_t metric, uint32_t us)
{
    if ((unsigned)metric >= UI_TIMING_COUNT) return;
    portENTER_CRITICAL(&s_timing_lock);
    record_locked(metric, us);
    portEXIT_CRITICAL(&s_timing_lock);
}

void ui_scanout_timing_wave_complete(uint8_t deck_index, uint32_t cache_us,
                                     uint32_t blit_us, int64_t now_us)
{
    if (deck_index > 1) return;
    ui_scanout_timing_metric_t base = deck_index ? UI_TIMING_D2_CACHE : UI_TIMING_D1_CACHE;
    portENTER_CRITICAL(&s_timing_lock);
    record_locked(base, cache_us);
    record_locked(base + 1, blit_us);
    record_locked(base + 2, elapsed_us(now_us, s_frame_refresh_us));
    portEXIT_CRITICAL(&s_timing_lock);
}

void ui_scanout_timing_snapshot(ui_scanout_timing_snapshot_t *out)
{
    if (!out) return;
    portENTER_CRITICAL(&s_timing_lock);
    *out = s_timing;
    portEXIT_CRITICAL(&s_timing_lock);
}
