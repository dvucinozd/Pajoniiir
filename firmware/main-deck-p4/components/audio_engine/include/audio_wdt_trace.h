#pragma once

#include <stdbool.h>
#include <stdint.h>

#define AUDIO_WDT_TRACE_MAGIC   0x41574454u /* "AWDT" */
#define AUDIO_WDT_TRACE_VERSION 1u

typedef enum {
    AUDIO_WDT_PHASE_NONE = 0,
    AUDIO_WDT_PHASE_WAIT_CODEC,
    AUDIO_WDT_PHASE_EOF_DRAIN,
    AUDIO_WDT_PHASE_SCRATCH_CONTROL,
    AUDIO_WDT_PHASE_COMMANDS,
    AUDIO_WDT_PHASE_SNAPSHOT,
    AUDIO_WDT_PHASE_IDLE_DELAY,
    AUDIO_WDT_PHASE_MIX_GROUP,
    AUDIO_WDT_PHASE_RECORDER,
    AUDIO_WDT_PHASE_MONITOR,
    AUDIO_WDT_PHASE_MAIN_I2S,
    AUDIO_WDT_PHASE_CODEC,
    AUDIO_WDT_PHASE_BOOK_LOCK,
    AUDIO_WDT_PHASE_DIAGNOSTICS,
    AUDIO_WDT_PHASE_YIELD,
    AUDIO_WDT_PHASE_EXIT,
} audio_wdt_phase_t;

/* Public decoded record. The retained journal uses a much smaller packed form
 * so marking one hot-path phase is only two aligned 32-bit stores. */
typedef struct {
    uint32_t magic;
    uint32_t magic_inv;
    uint32_t version;
    uint32_t version_inv;
    uint32_t sequence;
    uint32_t sequence_inv;
    uint32_t boot_id;
    uint32_t boot_id_inv;
    uint32_t phase;
    uint32_t phase_inv;
    uint32_t block;
    uint32_t block_inv;
    uint32_t mix_group;
    uint32_t mix_group_inv;
    uint32_t busy_blocks;
    uint32_t busy_blocks_inv;
    uint32_t active_deck_mask;
    uint32_t active_deck_mask_inv;
    uint32_t twdt_isr_seen;
    uint32_t twdt_isr_seen_inv;
    uint64_t entered_us;
    uint64_t entered_us_inv;
    uint64_t last_idle_us;
    uint64_t last_idle_us_inv;
} audio_wdt_trace_record_t;

typedef struct {
    uint32_t stamp;
    uint32_t stamp_inv;
} audio_wdt_trace_marker_t;

typedef struct {
    uint32_t magic;
    uint32_t magic_inv;
    uint32_t version_boot;
    uint32_t version_boot_inv;
    uint32_t block;
    uint32_t block_inv;
    uint32_t entered_us;
    uint32_t entered_us_inv;
    uint32_t last_idle_us;
    uint32_t last_idle_us_inv;
} audio_wdt_trace_context_t;

typedef struct {
    audio_wdt_trace_marker_t marker[2];
    audio_wdt_trace_context_t context[2];
    uint32_t twdt_isr_seen;
    uint32_t twdt_isr_seen_inv;
} audio_wdt_trace_journal_t;

void audio_wdt_trace_clear_watchdog_flags(
    volatile audio_wdt_trace_journal_t *journal);

void audio_wdt_trace_begin_block(volatile audio_wdt_trace_journal_t *journal,
                                 uint32_t boot_id,
                                 uint32_t block,
                                 uint64_t entered_us,
                                 uint64_t last_idle_us);

void audio_wdt_trace_mark(volatile audio_wdt_trace_journal_t *journal,
                          uint32_t sequence,
                          audio_wdt_phase_t phase,
                          uint32_t mix_group,
                          uint32_t busy_blocks,
                          uint32_t active_deck_mask);

bool audio_wdt_trace_read(const volatile audio_wdt_trace_journal_t *journal,
                          audio_wdt_trace_record_t *out_record);

bool audio_wdt_trace_record_valid(const audio_wdt_trace_record_t *record);
const char *audio_wdt_trace_phase_name(audio_wdt_phase_t phase);
