#pragma once

/* S3CP controller profile: parser + table-driven MIDI mapper runtime.
 *
 * Format specification: docs/CONTROLLER_PROFILE_SCHEMA.md
 * Compiler: tools/controller_profile/compile_profile.py
 *
 * Pure C99 with no ESP-IDF dependencies so the host test harness compiles it
 * directly. The future S3 runtime component wraps this with UART transfer and
 * USB MIDI glue; semantic (type, id, value) tuples match control_link.h.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CP_MAGIC            "S3CP"
#define CP_VERSION          2
#define CP_HEADER_SIZE      32
#define CP_INPUT_ENTRY_SIZE 16
#define CP_OUTPUT_ENTRY_SIZE 12

#define CP_MAX_INPUTS     320
#define CP_MAX_OUTPUTS    160
#define CP_MAX_PAIR_SLOTS 40

/* Parse result codes (0 = OK, negative = error). */
#define CP_OK          0
#define CP_ERR_ARG     -1
#define CP_ERR_MAGIC   -2
#define CP_ERR_VERSION -3
#define CP_ERR_SIZE    -4
#define CP_ERR_CRC     -5
#define CP_ERR_BOUNDS  -6

/* Input entry raw_type values. */
typedef enum {
    CP_IN_NOTE_BUTTON = 0,
    CP_IN_NOTE_VALUE = 1,
    CP_IN_CC_REL64 = 2,
    CP_IN_CC_REL_2C = 3,
    CP_IN_CC14_MSB = 4,
    CP_IN_CC14_LSB = 5,
    CP_IN_CC7_ABS = 6,
    CP_IN_NOTE_STATE_PAIR = 7,
} cp_raw_type_t;

/* Input entry flags. */
#define CP_IN_FLAG_REPLAY        0x0001
#define CP_IN_FLAG_PAIR_MEMBER_B 0x0002

/* Output entry kinds. */
typedef enum {
    CP_OUT_NOTE_ONOFF = 0,
    CP_OUT_CC_VALUE = 1,
} cp_out_kind_t;

#define CP_DECK_ANY 0xFF
#define CP_PAIR_SLOT_NONE 0xFF

/* Header capability flags. */
#define CP_PF_LED_FEEDBACK (1u << 0)
#define CP_PF_USB_AUDIO    (1u << 1)
#define CP_PF_JOG_TOUCH    (1u << 2)
#define CP_PF_PITCH_14BIT  (1u << 3)

typedef struct {
    uint8_t match_status;
    uint8_t match_data1;
    uint8_t raw_type;
    uint8_t pair_slot;
    uint8_t semantic_type;
    uint8_t semantic_id;
    uint16_t flags;
    int16_t base_value;
    uint16_t press_mask;
    int8_t lut[4];
} cp_input_entry_t;

typedef struct {
    uint8_t led_id;
    uint8_t deck;
    uint8_t out_kind;
    uint8_t status;
    uint8_t data1;
    uint8_t off_value;
    uint8_t on_value;
    uint8_t blink_value;
} cp_output_entry_t;

typedef struct {
    uint16_t vid;
    uint16_t pid;
    uint32_t flags;
    uint16_t input_count;
    uint16_t output_count;
    uint8_t pair_slot_count;
    uint8_t decks;
    cp_input_entry_t inputs[CP_MAX_INPUTS];
    cp_output_entry_t outputs[CP_MAX_OUTPUTS];
} cp_profile_t;

/* Semantic event, wire-compatible with control_link_send_semantic(). */
typedef struct {
    uint8_t type;
    uint8_t id;
    int16_t value;
} cp_event_t;

typedef struct {
    uint8_t msb;
    uint8_t lsb;
    bool msb_valid;
    bool lsb_valid;
    uint8_t pair_bits; /* NOTE_STATE_PAIR latched member bits */
} cp_pair_slot_t;

typedef struct {
    cp_pair_slot_t slots[CP_MAX_PAIR_SLOTS];
    /* Last emitted value per CC7_ABS entry index (replay support). */
    int16_t cc7_value[CP_MAX_INPUTS];
    bool cc7_valid[CP_MAX_INPUTS];
} cp_runtime_t;

typedef bool (*cp_event_emit_cb_t)(uint8_t type, uint8_t id, int16_t value,
                                   void *ctx);

/* CRC-32 (IEEE 802.3, zlib-compatible). */
uint32_t cp_crc32(const uint8_t *data, size_t len);

/* Parse and validate an S3CP blob into a profile. */
int cp_profile_parse(const uint8_t *data, size_t len, cp_profile_t *out);

void cp_runtime_init(cp_runtime_t *rt);

/* Map one 3-byte MIDI message through the profile. Returns true and fills
 * *out when the message produced a semantic event. */
bool cp_runtime_process(const cp_profile_t *profile, cp_runtime_t *rt,
                        uint8_t status, uint8_t data1, uint8_t data2,
                        cp_event_t *out);

/* Re-emit last known values of replay-flagged absolute controls (input
 * snapshot replay after P4 reconnect). Returns the number of events emitted;
 * stops early if the callback returns false. */
size_t cp_runtime_emit_snapshot(const cp_profile_t *profile,
                                const cp_runtime_t *rt,
                                cp_event_emit_cb_t cb, void *ctx);

/* Map a semantic LED frame to a 3-byte MIDI OUT message (status, data1,
 * data2). Returns false when the profile has no mapping for (led, deck). */
bool cp_profile_map_led(const cp_profile_t *profile, uint8_t led, uint8_t deck,
                        uint8_t state, uint8_t midi_out[3]);

#ifdef __cplusplus
}
#endif
