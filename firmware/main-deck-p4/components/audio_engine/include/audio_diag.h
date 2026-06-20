#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_DIAG_DEFAULT_REPORT_SAMPLES 120u

typedef struct {
    uint32_t samples;
    uint32_t last_us;
    uint32_t avg_us;
    uint32_t max_us;
} audio_diag_report_t;

typedef struct {
    uint32_t report_samples;
    uint32_t sample_count;
    uint64_t total_us;
    uint32_t max_us;
    uint32_t last_us;
} audio_diag_counter_t;

typedef struct {
    uint32_t threshold_us;
    uint32_t count;
    uint32_t max_us;
} audio_diag_late_counter_t;

void audio_diag_counter_init(audio_diag_counter_t *counter,
                             uint32_t report_samples);

bool audio_diag_record(audio_diag_counter_t *counter,
                       uint32_t duration_us,
                       audio_diag_report_t *out_report);

void audio_diag_late_counter_init(audio_diag_late_counter_t *counter,
                                  uint32_t threshold_us);

bool audio_diag_late_record(audio_diag_late_counter_t *counter,
                            uint32_t duration_us);

#ifdef __cplusplus
}
#endif
