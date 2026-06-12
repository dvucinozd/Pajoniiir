#include "ui_overview_perf.h"

void ui_overview_perf_counter_init(ui_overview_perf_counter_t *counter,
                                   uint32_t report_samples)
{
    if (!counter) {
        return;
    }

    counter->report_samples = report_samples > 0
                            ? report_samples
                            : UI_OVERVIEW_PERF_DEFAULT_REPORT_SAMPLES;
    counter->sample_count = 0;
    counter->total_us = 0;
    counter->max_us = 0;
    counter->last_us = 0;
}

bool ui_overview_perf_record(ui_overview_perf_counter_t *counter,
                             uint32_t duration_us,
                             ui_overview_perf_report_t *out_report)
{
    if (!counter) {
        return false;
    }

    if (counter->report_samples == 0) {
        ui_overview_perf_counter_init(counter, 0);
    }

    counter->sample_count++;
    counter->total_us += duration_us;
    counter->last_us = duration_us;
    if (duration_us > counter->max_us) {
        counter->max_us = duration_us;
    }

    if (counter->sample_count < counter->report_samples) {
        return false;
    }

    if (out_report) {
        out_report->samples = counter->sample_count;
        out_report->last_us = counter->last_us;
        out_report->avg_us = (uint32_t)(counter->total_us / counter->sample_count);
        out_report->max_us = counter->max_us;
    }

    uint32_t report_samples = counter->report_samples;
    ui_overview_perf_counter_init(counter, report_samples);
    return true;
}
