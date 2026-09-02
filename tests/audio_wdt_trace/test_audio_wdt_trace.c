#include "audio_wdt_trace.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_empty_and_complete_record(void)
{
    audio_wdt_trace_journal_t journal;
    memset(&journal, 0, sizeof(journal));
    audio_wdt_trace_record_t out;
    assert(!audio_wdt_trace_read(&journal, &out));

    audio_wdt_trace_clear_watchdog_flags(&journal);
    audio_wdt_trace_begin_block(&journal, 7u, 81u, 9000u, 1000u);
    audio_wdt_trace_mark(&journal, 1u, AUDIO_WDT_PHASE_MAIN_I2S,
                         0u, 17u, 3u);
    assert(audio_wdt_trace_read(&journal, &out));
    assert(out.sequence == 1u);
    assert(out.boot_id == 7u);
    assert(out.phase == AUDIO_WDT_PHASE_MAIN_I2S);
    assert(out.block == 81u);
    assert(out.busy_blocks == 17u);
    assert(out.active_deck_mask == 3u);
    assert(out.twdt_isr_seen == 0u);
    assert(out.entered_us - out.last_idle_us == 8000u);
}

static void test_newest_slot_and_torn_write_fallback(void)
{
    audio_wdt_trace_journal_t journal;
    memset(&journal, 0, sizeof(journal));
    audio_wdt_trace_clear_watchdog_flags(&journal);
    audio_wdt_trace_begin_block(&journal, 4u, 10u, 200u, 100u);
    audio_wdt_trace_mark(&journal, 2u, AUDIO_WDT_PHASE_MIX_GROUP,
                         5u, 9u, 3u);
    audio_wdt_trace_begin_block(&journal, 4u, 11u, 300u, 100u);
    audio_wdt_trace_mark(&journal, 3u, AUDIO_WDT_PHASE_BOOK_LOCK,
                         0u, 10u, 3u);
    audio_wdt_trace_record_t out;
    assert(audio_wdt_trace_read(&journal, &out));
    assert(out.sequence == 3u);
    assert(out.phase == AUDIO_WDT_PHASE_BOOK_LOCK);

    journal.marker[1].stamp_inv ^= 1u; /* torn latest phase marker */
    assert(audio_wdt_trace_read(&journal, &out));
    assert(out.sequence == 2u);
    assert(out.phase == AUDIO_WDT_PHASE_MIX_GROUP);
    assert(out.mix_group == 5u);
}

static void test_new_boot_wins_over_larger_old_block(void)
{
    audio_wdt_trace_journal_t journal;
    memset(&journal, 0, sizeof(journal));
    audio_wdt_trace_clear_watchdog_flags(&journal);
    audio_wdt_trace_begin_block(&journal, 8u, 41001u, 1000u, 900u);
    audio_wdt_trace_mark(&journal, 10u, AUDIO_WDT_PHASE_EXIT, 0u, 0u, 0u);
    audio_wdt_trace_begin_block(&journal, 9u, 0u, 2000u, 1900u);
    audio_wdt_trace_mark(&journal, 11u, AUDIO_WDT_PHASE_NONE, 0u, 0u, 0u);

    audio_wdt_trace_record_t out;
    assert(audio_wdt_trace_read(&journal, &out));
    assert(out.boot_id == 9u);
    assert(out.block == 0u);
    assert(out.phase == AUDIO_WDT_PHASE_NONE);
}

static void test_retained_task_wdt_marker(void)
{
    audio_wdt_trace_journal_t journal;
    memset(&journal, 0, sizeof(journal));
    audio_wdt_trace_clear_watchdog_flags(&journal);
    audio_wdt_trace_begin_block(&journal, 3u, 5u, 100u, 90u);
    audio_wdt_trace_mark(&journal, 1u, AUDIO_WDT_PHASE_CODEC, 0u, 1u, 1u);
    journal.twdt_isr_seen_inv = ~AUDIO_WDT_TRACE_MAGIC;
    journal.twdt_isr_seen = AUDIO_WDT_TRACE_MAGIC;

    audio_wdt_trace_record_t out;
    assert(audio_wdt_trace_read(&journal, &out));
    assert(out.twdt_isr_seen == 1u);
    audio_wdt_trace_clear_watchdog_flags(&journal);
    assert(audio_wdt_trace_read(&journal, &out));
    assert(out.twdt_isr_seen == 0u);
}

static void test_phase_names_are_stable(void)
{
    assert(strcmp(audio_wdt_trace_phase_name(AUDIO_WDT_PHASE_MONITOR),
                  "monitor") == 0);
    assert(strcmp(audio_wdt_trace_phase_name(AUDIO_WDT_PHASE_MAIN_I2S),
                  "main_i2s") == 0);
    assert(strcmp(audio_wdt_trace_phase_name((audio_wdt_phase_t)99),
                  "invalid") == 0);
}

int main(void)
{
    test_empty_and_complete_record();
    test_newest_slot_and_torn_write_fallback();
    test_new_boot_wins_over_larger_old_block();
    test_retained_task_wdt_marker();
    test_phase_names_are_stable();
    puts("audio_wdt_trace tests passed");
    return 0;
}
