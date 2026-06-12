#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UI_OVERVIEW_PERF_DEFAULT_REPORT_SAMPLES 60u

typedef struct {
    uint32_t samples;
    uint32_t last_us;
    uint32_t avg_us;
    uint32_t max_us;
} ui_overview_perf_report_t;

typedef struct {
    uint32_t report_samples;
    uint32_t sample_count;
    uint64_t total_us;
    uint32_t max_us;
    uint32_t last_us;
} ui_overview_perf_counter_t;

void ui_overview_perf_counter_init(ui_overview_perf_counter_t *counter,
                                   uint32_t report_samples);

bool ui_overview_perf_record(ui_overview_perf_counter_t *counter,
                             uint32_t duration_us,
                             ui_overview_perf_report_t *out_report);

#ifdef __cplusplus
}
#endif
