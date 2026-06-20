#include "audio_diag.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static void test_counter_reports_after_configured_samples(void)
{
    audio_diag_counter_t counter;
    audio_diag_counter_init(&counter, 3);

    audio_diag_report_t report;
    assert(!audio_diag_record(&counter, 100, &report));
    assert(!audio_diag_record(&counter, 200, &report));
    assert(audio_diag_record(&counter, 500, &report));

    assert(report.samples == 3);
    assert(report.last_us == 500);
    assert(report.avg_us == 266);
    assert(report.max_us == 500);
}

static void test_counter_uses_default_report_period(void)
{
    audio_diag_counter_t counter;
    audio_diag_counter_init(&counter, 0);

    assert(counter.report_samples == AUDIO_DIAG_DEFAULT_REPORT_SAMPLES);
}

static void test_late_counter_counts_threshold_crossings(void)
{
    audio_diag_late_counter_t late;
    audio_diag_late_counter_init(&late, 2000);

    assert(!audio_diag_late_record(&late, 1999));
    assert(audio_diag_late_record(&late, 2000));
    assert(audio_diag_late_record(&late, 5000));
    assert(late.count == 2);
    assert(late.max_us == 5000);
}

int main(void)
{
    test_counter_reports_after_configured_samples();
    test_counter_uses_default_report_period();
    test_late_counter_counts_threshold_crossings();
    puts("audio_diag tests passed");
    return 0;
}
