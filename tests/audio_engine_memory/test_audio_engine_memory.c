#include "audio_engine_memory.h"
#include "esp_heap_caps.h"
#include "service_log.h"
#include <assert.h>
#include <stdio.h>

static int64_t clock_us;
static uint32_t delay_us, records, walks;
uint32_t esp_get_free_heap_size(void) { return 90000; }
int64_t esp_timer_get_time(void) { return clock_us; }
size_t heap_caps_get_free_size(uint32_t caps) { return 10000 * caps; }
size_t heap_caps_get_minimum_free_size(uint32_t caps) { return 1000 * caps; }
size_t heap_caps_get_largest_free_block(uint32_t caps)
{
    assert(caps != MALLOC_CAP_SPIRAM); /* idle and active sampling must be safe */
    ++walks;
    clock_us += delay_us;
    return 100 * caps;
}
void service_log_event(service_log_event_t event, service_log_severity_t severity,
                       uint8_t n, uint32_t index, uint32_t elapsed,
                       uint32_t start_ms, uint32_t largest, const char *text)
{
    assert(event == SERVICE_LOG_HEAP_WALK_SLOW && severity == SERVICE_LOG_WARN);
    assert(n == 4 && index < 3 && elapsed == delay_us && elapsed >= 500);
    assert(start_ms == (uint32_t)((clock_us - delay_us) / 1000));
    assert(largest != 0 && text == NULL);
    ++records;
}
int main(void)
{
    audio_engine_diagnostics_snapshot_t d = {0};
    d.psram_largest_free = 123; /* never leak a previous measured value */
    d.heap_walk_max_us[2] = 999;
    audio_engine_snapshot_memory(NULL);
    assert(walks == 0);
    delay_us = 499;
    audio_engine_snapshot_memory(&d);
    assert(records == 0 && walks == 2);
    assert(d.heap_free == 90000 && d.internal_free == 10000);
    assert(d.dma_min_free == 3000 && d.psram_largest_free == 0);
    assert(d.psram_free == 40000 && d.psram_min_free == 4000);
    assert(d.heap_walk_max_us[0] == 499 && d.heap_walk_max_us[2] == 0);
    delay_us = 1200;
    audio_engine_snapshot_memory(&d);
    assert(records == 2 && d.heap_walk_max_us[1] == 1200);
    for (int i = 0; i < 10; ++i) audio_engine_snapshot_memory(&d);
    assert(records == 4 && walks == 24);
    delay_us = 10;
    audio_engine_snapshot_memory(&d);
    assert(records == 4 && d.heap_walk_max_us[0] == 1200);
    puts("heap diagnostics timing, values and bounded records: PASS");
    return 0;
}
