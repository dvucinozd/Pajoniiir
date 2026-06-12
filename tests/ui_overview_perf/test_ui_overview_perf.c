#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "ui_overview_perf.h"

static void test_records_report_after_configured_sample_count(void)
{
    ui_overview_perf_counter_t counter;
    ui_overview_perf_counter_init(&counter, 3);

    ui_overview_perf_report_t report = {0};
    assert(!ui_overview_perf_record(&counter, 100, &report));
    assert(!ui_overview_perf_record(&counter, 200, &report));
    assert(ui_overview_perf_record(&counter, 400, &report));

    assert(report.samples == 3);
    assert(report.last_us == 400);
    assert(report.avg_us == 233);
    assert(report.max_us == 400);
}

static void test_report_window_resets_after_emit(void)
{
    ui_overview_perf_counter_t counter;
    ui_overview_perf_counter_init(&counter, 2);

    ui_overview_perf_report_t report = {0};
    assert(!ui_overview_perf_record(&counter, 100, &report));
    assert(ui_overview_perf_record(&counter, 300, &report));
    assert(report.avg_us == 200);
    assert(report.max_us == 300);

    assert(!ui_overview_perf_record(&counter, 50, &report));
    assert(ui_overview_perf_record(&counter, 70, &report));
    assert(report.samples == 2);
    assert(report.last_us == 70);
    assert(report.avg_us == 60);
    assert(report.max_us == 70);
}

static void test_zero_interval_uses_default_report_interval(void)
{
    ui_overview_perf_counter_t counter;
    ui_overview_perf_counter_init(&counter, 0);

    ui_overview_perf_report_t report = {0};
    for (uint32_t i = 0; i < UI_OVERVIEW_PERF_DEFAULT_REPORT_SAMPLES - 1; i++) {
        assert(!ui_overview_perf_record(&counter, 10, &report));
    }
    assert(ui_overview_perf_record(&counter, 20, &report));
    assert(report.samples == UI_OVERVIEW_PERF_DEFAULT_REPORT_SAMPLES);
    assert(report.last_us == 20);
}

int main(void)
{
    test_records_report_after_configured_sample_count();
    test_report_window_resets_after_emit();
    test_zero_interval_uses_default_report_interval();

    puts("ui_overview_perf tests passed");
    return 0;
}
