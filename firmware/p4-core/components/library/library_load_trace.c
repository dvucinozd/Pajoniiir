#include "library_load_trace.h"

#include <string.h>

#if !defined(LIBRARY_LOAD_TRACE_HOST_TEST)
#include "esp_attr.h"
#include "esp_timer.h"
#define TRACE_NOINIT __NOINIT_ATTR
#else
#define TRACE_NOINIT
static uint32_t s_test_time_us;
#endif

typedef struct {
    library_load_trace_record_t slot[2];
} library_load_trace_journal_t;

static TRACE_NOINIT volatile library_load_trace_journal_t s_journal;
static library_load_trace_record_t s_previous;
static bool s_previous_valid;
static bool s_initialized;
static uint32_t s_sequence;
static uint32_t s_boot_id;

static uint32_t now_us(void)
{
#if defined(LIBRARY_LOAD_TRACE_HOST_TEST)
    return ++s_test_time_us;
#else
    return (uint32_t)esp_timer_get_time();
#endif
}

static bool pair32(uint32_t value, uint32_t inverse)
{
    return inverse == ~value;
}

static bool record_valid(const library_load_trace_record_t *record)
{
    return record && record->magic == LIBRARY_LOAD_TRACE_MAGIC &&
           pair32(record->magic, record->magic_inv) &&
           record->version == LIBRARY_LOAD_TRACE_VERSION &&
           pair32(record->version, record->version_inv) &&
           pair32(record->sequence, record->sequence_inv) &&
           pair32(record->boot_id, record->boot_id_inv) &&
           pair32(record->phase, record->phase_inv) &&
           record->phase <= LIBRARY_LOAD_PHASE_FAILED &&
           pair32(record->track_key, record->track_key_inv) &&
           pair32(record->entered_us, record->entered_us_inv);
}

static bool sequence_is_newer(uint32_t candidate, uint32_t reference)
{
    return (int32_t)(candidate - reference) > 0;
}

static bool journal_read(library_load_trace_record_t *out)
{
    library_load_trace_record_t record[2];
    memcpy(&record[0], (const void *)&s_journal.slot[0], sizeof(record[0]));
    memcpy(&record[1], (const void *)&s_journal.slot[1], sizeof(record[1]));
    bool valid[2] = {record_valid(&record[0]), record_valid(&record[1])};
    if (!valid[0] && !valid[1]) return false;
    unsigned selected = valid[1] &&
        (!valid[0] || sequence_is_newer(record[1].sequence,
                                        record[0].sequence)) ? 1u : 0u;
    if (out) *out = record[selected];
    return true;
}

void library_load_trace_mark(library_load_phase_t phase, uint32_t track_key)
{
    if (!s_initialized) library_load_trace_boot_init();
    library_load_trace_record_t next = {
        .magic = 0u,
        .magic_inv = UINT32_MAX,
        .version = LIBRARY_LOAD_TRACE_VERSION,
        .version_inv = ~LIBRARY_LOAD_TRACE_VERSION,
        .sequence = ++s_sequence,
        .sequence_inv = ~s_sequence,
        .boot_id = s_boot_id,
        .boot_id_inv = ~s_boot_id,
        .phase = (uint32_t)phase,
        .phase_inv = ~(uint32_t)phase,
        .track_key = track_key,
        .track_key_inv = ~track_key,
        .entered_us = now_us(),
    };
    next.entered_us_inv = ~next.entered_us;
    volatile library_load_trace_record_t *slot =
        &s_journal.slot[next.sequence & 1u];
    slot->magic = 0u;
    slot->magic_inv = UINT32_MAX;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    memcpy((uint8_t *)(void *)slot + 2u * sizeof(uint32_t),
           (const uint8_t *)&next + 2u * sizeof(uint32_t),
           sizeof(next) - 2u * sizeof(uint32_t));
    __atomic_thread_fence(__ATOMIC_RELEASE);
    slot->magic_inv = ~LIBRARY_LOAD_TRACE_MAGIC;
    slot->magic = LIBRARY_LOAD_TRACE_MAGIC;
}

void library_load_trace_boot_init(void)
{
    if (s_initialized) return;
    s_previous_valid = journal_read(&s_previous);
    s_sequence = s_previous_valid ? s_previous.sequence : 0u;
    s_boot_id = s_previous_valid ? s_previous.boot_id + 1u : 1u;
    s_initialized = true;
    library_load_trace_mark(LIBRARY_LOAD_PHASE_IDLE, 0u);
}

void library_load_trace_snapshot(bool *out_previous_valid,
                                 library_load_trace_record_t *out_previous,
                                 bool *out_current_valid,
                                 library_load_trace_record_t *out_current)
{
    library_load_trace_boot_init();
    if (out_previous_valid) *out_previous_valid = s_previous_valid;
    if (out_previous) {
        memset(out_previous, 0, sizeof(*out_previous));
        if (s_previous_valid) *out_previous = s_previous;
    }
    bool current_valid = journal_read(out_current);
    if (out_current_valid) *out_current_valid = current_valid;
    if (out_current && !current_valid) memset(out_current, 0, sizeof(*out_current));
}

const char *library_load_trace_phase_name(library_load_phase_t phase)
{
    static const char *const names[] = {
        "idle", "resolve", "cache_usb_stat", "cache_sd_gate_wait",
        "cache_sd_gate_held", "cache_media_gate_wait", "cache_dat_stat",
        "cache_ext_stat", "cache_sd_open", "cache_sd_read", "usb_dat",
        "usb_ext", "cache_save_usb_stat",
        "cache_save_sd_open", "cache_save_header", "cache_save_low",
        "cache_save_vbr", "cache_save_cues", "cache_save_beats",
        "cache_save_high", "cache_save_close", "cache_save_replace",
        "publish_lock", "free_old", "done", "failed"
    };
    return (unsigned)phase < sizeof(names) / sizeof(names[0])
        ? names[(unsigned)phase] : "invalid";
}

#if defined(LIBRARY_LOAD_TRACE_HOST_TEST)
void library_load_trace_test_reset_all(void)
{
    memset((void *)&s_journal, 0, sizeof(s_journal));
    memset(&s_previous, 0, sizeof(s_previous));
    s_previous_valid = false;
    s_initialized = false;
    s_sequence = 0u;
    s_boot_id = 0u;
    s_test_time_us = 0u;
}

void library_load_trace_test_reboot(void)
{
    memset(&s_previous, 0, sizeof(s_previous));
    s_previous_valid = false;
    s_initialized = false;
    s_sequence = 0u;
    s_boot_id = 0u;
}
#endif
