#pragma once

#include <stdbool.h>
#include <stdint.h>

#define LIBRARY_LOAD_TRACE_MAGIC   0x4C4C4454u /* "LLDT" */
#define LIBRARY_LOAD_TRACE_VERSION 1u

typedef enum {
    LIBRARY_LOAD_PHASE_IDLE = 0,
    LIBRARY_LOAD_PHASE_RESOLVE,
    LIBRARY_LOAD_PHASE_CACHE_USB_STAT,
    LIBRARY_LOAD_PHASE_CACHE_SD_GATE_WAIT,
    LIBRARY_LOAD_PHASE_CACHE_SD_GATE_HELD,
    LIBRARY_LOAD_PHASE_CACHE_MEDIA_GATE_WAIT,
    LIBRARY_LOAD_PHASE_CACHE_DAT_STAT,
    LIBRARY_LOAD_PHASE_CACHE_EXT_STAT,
    LIBRARY_LOAD_PHASE_CACHE_SD_OPEN,
    LIBRARY_LOAD_PHASE_CACHE_SD_READ,
    LIBRARY_LOAD_PHASE_USB_DAT,
    LIBRARY_LOAD_PHASE_USB_EXT,
    LIBRARY_LOAD_PHASE_CACHE_SAVE_USB_STAT,
    LIBRARY_LOAD_PHASE_CACHE_SAVE_SD_OPEN,
    LIBRARY_LOAD_PHASE_CACHE_SAVE_HEADER,
    LIBRARY_LOAD_PHASE_CACHE_SAVE_LOW,
    LIBRARY_LOAD_PHASE_CACHE_SAVE_VBR,
    LIBRARY_LOAD_PHASE_CACHE_SAVE_CUES,
    LIBRARY_LOAD_PHASE_CACHE_SAVE_BEATS,
    LIBRARY_LOAD_PHASE_CACHE_SAVE_HIGH,
    LIBRARY_LOAD_PHASE_CACHE_SAVE_CLOSE,
    LIBRARY_LOAD_PHASE_CACHE_SAVE_REPLACE,
    LIBRARY_LOAD_PHASE_PUBLISH_LOCK,
    LIBRARY_LOAD_PHASE_FREE_OLD,
    LIBRARY_LOAD_PHASE_DONE,
    LIBRARY_LOAD_PHASE_FAILED,
} library_load_phase_t;

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
    uint32_t track_key;
    uint32_t track_key_inv;
    uint32_t entered_us;
    uint32_t entered_us_inv;
} library_load_trace_record_t;

void library_load_trace_boot_init(void);
void library_load_trace_mark(library_load_phase_t phase, uint32_t track_key);
void library_load_trace_snapshot(bool *out_previous_valid,
                                 library_load_trace_record_t *out_previous,
                                 bool *out_current_valid,
                                 library_load_trace_record_t *out_current);
const char *library_load_trace_phase_name(library_load_phase_t phase);

#if defined(LIBRARY_LOAD_TRACE_HOST_TEST)
void library_load_trace_test_reset_all(void);
void library_load_trace_test_reboot(void);
#endif
