#include "audio_wdt_trace.h"

#include <string.h>

#define MARKER_SEQUENCE_MASK 0xFFFFu
#define MARKER_PHASE_SHIFT   16u
#define MARKER_GROUP_SHIFT   20u
#define MARKER_BUSY_SHIFT    24u
#define MARKER_ACTIVE_SHIFT  30u

static bool pair32(uint32_t value, uint32_t inverse)
{
    return inverse == ~value;
}

static uint32_t marker_pack(uint32_t sequence, audio_wdt_phase_t phase,
                            uint32_t mix_group, uint32_t busy_blocks,
                            uint32_t active_deck_mask)
{
    if (mix_group > 15u) mix_group = 15u;
    if (busy_blocks > 63u) busy_blocks = 63u;
    return (sequence & MARKER_SEQUENCE_MASK) |
           (((uint32_t)phase & 0x0Fu) << MARKER_PHASE_SHIFT) |
           ((mix_group & 0x0Fu) << MARKER_GROUP_SHIFT) |
           ((busy_blocks & 0x3Fu) << MARKER_BUSY_SHIFT) |
           ((active_deck_mask & 0x03u) << MARKER_ACTIVE_SHIFT);
}

static bool sequence_is_newer(uint16_t candidate, uint16_t reference)
{
    return (int16_t)(candidate - reference) > 0;
}

static bool boot_is_newer(uint32_t candidate, uint32_t reference)
{
    uint32_t delta = (candidate - reference) & 0x00FFFFFFu;
    return delta != 0u && delta < 0x00800000u;
}

static bool context_is_newer(const audio_wdt_trace_record_t *candidate,
                             const audio_wdt_trace_record_t *reference)
{
    if (candidate->boot_id != reference->boot_id) {
        return boot_is_newer(candidate->boot_id, reference->boot_id);
    }
    return (int32_t)(candidate->block - reference->block) > 0;
}

void audio_wdt_trace_clear_watchdog_flags(
    volatile audio_wdt_trace_journal_t *journal)
{
    if (!journal) return;
    journal->twdt_isr_seen_inv = UINT32_MAX;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    journal->twdt_isr_seen = 0u;
}

void audio_wdt_trace_begin_block(volatile audio_wdt_trace_journal_t *journal,
                                 uint32_t boot_id,
                                 uint32_t block,
                                 uint64_t entered_us,
                                 uint64_t last_idle_us)
{
    if (!journal) return;
    volatile audio_wdt_trace_context_t *ctx = &journal->context[block & 1u];
    ctx->magic = 0u;
    ctx->magic_inv = UINT32_MAX;
    uint32_t version_boot = (AUDIO_WDT_TRACE_VERSION << 24) |
                            (boot_id & 0x00FFFFFFu);
    ctx->version_boot = version_boot;
    ctx->version_boot_inv = ~version_boot;
    ctx->block = block;
    ctx->block_inv = ~block;
    ctx->entered_us = (uint32_t)entered_us;
    ctx->entered_us_inv = ~(uint32_t)entered_us;
    ctx->last_idle_us = (uint32_t)last_idle_us;
    ctx->last_idle_us_inv = ~(uint32_t)last_idle_us;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    ctx->magic_inv = ~AUDIO_WDT_TRACE_MAGIC;
    ctx->magic = AUDIO_WDT_TRACE_MAGIC;
}

void audio_wdt_trace_mark(volatile audio_wdt_trace_journal_t *journal,
                          uint32_t sequence,
                          audio_wdt_phase_t phase,
                          uint32_t mix_group,
                          uint32_t busy_blocks,
                          uint32_t active_deck_mask)
{
    if (!journal) return;
    uint32_t stamp = marker_pack(sequence, phase, mix_group, busy_blocks,
                                 active_deck_mask);
    volatile audio_wdt_trace_marker_t *slot = &journal->marker[sequence & 1u];
    slot->stamp_inv = ~stamp;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    slot->stamp = stamp;
}

static bool read_marker(const volatile audio_wdt_trace_journal_t *journal,
                        uint32_t *out_stamp)
{
    uint32_t stamp[2] = { journal->marker[0].stamp, journal->marker[1].stamp };
    uint32_t inv[2] = { journal->marker[0].stamp_inv, journal->marker[1].stamp_inv };
    bool valid[2] = { pair32(stamp[0], inv[0]), pair32(stamp[1], inv[1]) };
    if (!valid[0] && !valid[1]) return false;
    unsigned selected = valid[1] && (!valid[0] ||
        sequence_is_newer((uint16_t)stamp[1], (uint16_t)stamp[0])) ? 1u : 0u;
    *out_stamp = stamp[selected];
    return true;
}

static bool read_context(const volatile audio_wdt_trace_journal_t *journal,
                         unsigned index, audio_wdt_trace_record_t *out)
{
    const volatile audio_wdt_trace_context_t *ctx = &journal->context[index];
    uint32_t magic = ctx->magic;
    uint32_t version_boot = ctx->version_boot;
    uint32_t block = ctx->block;
    uint32_t entered = ctx->entered_us;
    uint32_t idle = ctx->last_idle_us;
    if (magic != AUDIO_WDT_TRACE_MAGIC || !pair32(magic, ctx->magic_inv) ||
        !pair32(version_boot, ctx->version_boot_inv) ||
        !pair32(block, ctx->block_inv) ||
        !pair32(entered, ctx->entered_us_inv) ||
        !pair32(idle, ctx->last_idle_us_inv) ||
        (version_boot >> 24) != AUDIO_WDT_TRACE_VERSION) {
        return false;
    }
    out->magic = magic;
    out->magic_inv = ~magic;
    out->version = version_boot >> 24;
    out->version_inv = ~out->version;
    out->boot_id = version_boot & 0x00FFFFFFu;
    out->boot_id_inv = ~out->boot_id;
    out->block = block;
    out->block_inv = ~block;
    out->entered_us = entered;
    out->entered_us_inv = ~out->entered_us;
    out->last_idle_us = idle;
    out->last_idle_us_inv = ~out->last_idle_us;
    return true;
}

bool audio_wdt_trace_read(const volatile audio_wdt_trace_journal_t *journal,
                          audio_wdt_trace_record_t *out_record)
{
    if (!journal || !out_record) return false;
    memset(out_record, 0, sizeof(*out_record));
    uint32_t stamp = 0u;
    if (!read_marker(journal, &stamp)) return false;

    audio_wdt_trace_record_t ctx[2];
    memset(ctx, 0, sizeof(ctx));
    bool valid[2] = { read_context(journal, 0u, &ctx[0]),
                      read_context(journal, 1u, &ctx[1]) };
    if (!valid[0] && !valid[1]) return false;
    unsigned selected = valid[1] &&
        (!valid[0] || context_is_newer(&ctx[1], &ctx[0]))
        ? 1u : 0u;
    *out_record = ctx[selected];
    out_record->sequence = stamp & MARKER_SEQUENCE_MASK;
    out_record->sequence_inv = ~out_record->sequence;
    out_record->phase = (stamp >> MARKER_PHASE_SHIFT) & 0x0Fu;
    out_record->phase_inv = ~out_record->phase;
    out_record->mix_group = (stamp >> MARKER_GROUP_SHIFT) & 0x0Fu;
    out_record->mix_group_inv = ~out_record->mix_group;
    out_record->busy_blocks = (stamp >> MARKER_BUSY_SHIFT) & 0x3Fu;
    out_record->busy_blocks_inv = ~out_record->busy_blocks;
    out_record->active_deck_mask = (stamp >> MARKER_ACTIVE_SHIFT) & 0x03u;
    out_record->active_deck_mask_inv = ~out_record->active_deck_mask;
    uint32_t twdt_seen = journal->twdt_isr_seen;
    uint32_t twdt_seen_inv = journal->twdt_isr_seen_inv;
    out_record->twdt_isr_seen = pair32(twdt_seen, twdt_seen_inv) &&
                                twdt_seen == AUDIO_WDT_TRACE_MAGIC;
    out_record->twdt_isr_seen_inv = ~out_record->twdt_isr_seen;
    return audio_wdt_trace_record_valid(out_record);
}

bool audio_wdt_trace_record_valid(const audio_wdt_trace_record_t *record)
{
    return record && record->magic == AUDIO_WDT_TRACE_MAGIC &&
           pair32(record->magic, record->magic_inv) &&
           record->version == AUDIO_WDT_TRACE_VERSION &&
           pair32(record->version, record->version_inv) &&
           pair32(record->sequence, record->sequence_inv) &&
           pair32(record->boot_id, record->boot_id_inv) &&
           pair32(record->phase, record->phase_inv) &&
           record->phase <= AUDIO_WDT_PHASE_EXIT &&
           pair32(record->block, record->block_inv) &&
           pair32(record->mix_group, record->mix_group_inv) &&
           pair32(record->busy_blocks, record->busy_blocks_inv) &&
           pair32(record->active_deck_mask, record->active_deck_mask_inv) &&
           pair32(record->twdt_isr_seen, record->twdt_isr_seen_inv) &&
           record->entered_us_inv == ~record->entered_us &&
           record->last_idle_us_inv == ~record->last_idle_us;
}

const char *audio_wdt_trace_phase_name(audio_wdt_phase_t phase)
{
    static const char *const names[] = {
        "none", "wait_codec", "eof_drain", "scratch_control", "commands",
        "snapshot", "idle_delay", "mix_group", "recorder", "monitor",
        "main_i2s", "codec", "book_lock", "diagnostics", "yield", "exit"
    };
    return (unsigned)phase < (sizeof(names) / sizeof(names[0]))
        ? names[(unsigned)phase] : "invalid";
}
