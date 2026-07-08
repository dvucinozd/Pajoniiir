#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// ─── Wire protocol (shared with ESP32-S3) ─────────────────────────────────────
//
// Frame layout (7 bytes):
//   [0] 0xA5        start byte
//   [1] type        CTRL_TYPE_* below
//   [2] id          button / encoder / led id
//   [3] val_lo      value LSB
//   [4] val_hi      value MSB
//   [5] seq         rolling sequence 0–255
//   [6] checksum    XOR of bytes [1]..[5]

#define CTRL_FRAME_LEN    7
#define CTRL_FRAME_START  0xA5

// S3 → P4
#define CTRL_TYPE_BUTTON     0x01  // id = button_id_t,  val = 0 released / 1 pressed
#define CTRL_TYPE_ENCODER    0x02  // id = 0 jog,        val = signed delta
#define CTRL_TYPE_PITCH      0x03  // id = 0,            val = 0–16383
#define CTRL_TYPE_HEARTBEAT  0x04  // id = 0,            val = firmware uptime s

// P4 → S3
#define CTRL_TYPE_LED        0x81  // id = led_id_t, val = 0 off / 1 on / 2 blink
#define CTRL_TYPE_STATE      0x82  // reserved

// ─── Button IDs (must match ESP32-S3 button_id_t) ────────────────────────────

typedef enum {
    BTN_EJECT = 0,
    BTN_TRACK_PREV,
    BTN_TRACK_NEXT,
    BTN_SEARCH_BACK,
    BTN_SEARCH_FWD,
    BTN_CUE,
    BTN_PLAY,
    BTN_PERF1,         // Jet
    BTN_PERF2,         // Zip
    BTN_PERF3,         // Wah
    BTN_HOLD,          // Hold (4th Digital Jog Break)
    BTN_MODE,          // Time / Auto Cue
    BTN_MASTER_TEMPO,
    BTN_LOAD,          // Load selected library track
    BTN_COUNT,
} button_id_t;

// ─── LED IDs (must match ESP32-S3 led_id_t) ──────────────────────────────────

typedef enum {
    LED_CUE = 0,
    LED_PLAY,
    LED_BEAT,
    LED_END,
    LED_PFL,
    LED_VU_METER,
    LED_COUNT,
    LED_PAD_MODE_HOT_CUE = LED_COUNT,
    LED_PAD_MODE_KEYBOARD,
    LED_PAD_MODE_PAD_FX1,
    LED_PAD_MODE_PAD_FX2,
    LED_PAD_MODE_BEAT_JUMP,
    LED_PAD_MODE_BEAT_LOOP,
    LED_PAD_MODE_SAMPLER,
    LED_PAD_MODE_KEY_SHIFT,
    LED_SYNC,
    LED_LOOP_IN,
    LED_LOOP_OUT,
    LED_BEAT_LOOP_PAD_1,
    LED_BEAT_LOOP_PAD_2,
    LED_BEAT_LOOP_PAD_3,
    LED_BEAT_LOOP_PAD_4,
    LED_BEAT_LOOP_PAD_5,
    LED_BEAT_LOOP_PAD_6,
    LED_BEAT_LOOP_PAD_7,
    LED_BEAT_LOOP_PAD_8,
    LED_PAD_FX1_PAD_1,
    LED_PAD_FX1_PAD_2,
    LED_PAD_FX1_PAD_3,
    LED_PAD_FX1_PAD_4,
    LED_PAD_FX1_PAD_5,
    LED_PAD_FX1_PAD_6,
    LED_PAD_FX1_PAD_7,
    LED_PAD_FX1_PAD_8,
    LED_PAD_FX2_PAD_1,
    LED_PAD_FX2_PAD_2,
    LED_PAD_FX2_PAD_3,
    LED_PAD_FX2_PAD_4,
    LED_PAD_FX2_PAD_5,
    LED_PAD_FX2_PAD_6,
    LED_PAD_FX2_PAD_7,
    LED_PAD_FX2_PAD_8,
    LED_SMART_CFX,
    LED_SMART_FADER,
    LED_BEAT_FX_ON,
    LED_HOT_CUE_PAD_1,
    LED_HOT_CUE_PAD_2,
    LED_HOT_CUE_PAD_3,
    LED_HOT_CUE_PAD_4,
    LED_HOT_CUE_PAD_5,
    LED_HOT_CUE_PAD_6,
    LED_HOT_CUE_PAD_7,
    LED_HOT_CUE_PAD_8,
    LED_MASTER_CUE,
    /* State-driven: published via the periodic FLX4 LED snapshot from
     * deck_state_t.censor_active (flx4_led_snapshot.c). */
    LED_CENSOR,
    /* Output-only capability below: MIDI note mapping exists in flx4_led_midi.c
     * and is packet-tested, but no deck handler drives these yet. Reserved for
     * the momentary press/release feedback in the gap-closure plan; intentionally
     * NOT part of the state snapshot. */
    LED_CUE_SHIFT,
    LED_LOOP_ADJUST_IN,
    LED_LOOP_ADJUST_OUT,
    LED_TRACK_LOAD_DECK1,
    LED_TRACK_LOAD_DECK2,
    /* Beat Jump pad LEDs are state-driven FLX4 pad feedback published by
     * deck_core outside the fixed FLX4 LED snapshot table. */
    LED_BEAT_JUMP_PAD_1,
    LED_BEAT_JUMP_PAD_2,
    LED_BEAT_JUMP_PAD_3,
    LED_BEAT_JUMP_PAD_4,
    LED_BEAT_JUMP_PAD_5,
    LED_BEAT_JUMP_PAD_6,
    LED_BEAT_JUMP_PAD_7,
    LED_BEAT_JUMP_PAD_8,
    LED_BEAT_JUMP_SHIFT_HELPER_7,
    LED_BEAT_JUMP_SHIFT_HELPER_8,
    LED_REMOTE_COUNT,
} led_id_t;

// ─── DDJ-FLX4 deck-aware semantic IDs ────────────────────────────────────────

typedef enum {
    CTRL_DECK_1 = 0,
    CTRL_DECK_2 = 1,
    CTRL_DECK_NONE = 0xFF,
} ctrl_deck_t;

typedef enum {
    CTRL_DECK_CTL_PLAY = 0,
    CTRL_DECK_CTL_CUE,
    CTRL_DECK_CTL_JOG_SCRATCH,
    CTRL_DECK_CTL_JOG_BEND,
    CTRL_DECK_CTL_JOG_TOUCH,
    CTRL_DECK_CTL_TEMPO,
    CTRL_DECK_CTL_SHIFT,
    CTRL_DECK_CTL_TO_START,
    CTRL_DECK_CTL_SYNC,
    CTRL_DECK_CTL_TEMPO_RANGE,
    CTRL_DECK_CTL_LOOP_IN,
    CTRL_DECK_CTL_LOOP_OUT,
    CTRL_DECK_CTL_RELOOP_EXIT,
    CTRL_DECK_CTL_LOOP_HALVE,
    CTRL_DECK_CTL_LOOP_DOUBLE,
    CTRL_DECK_CTL_BEAT_JUMP_BACK,
    CTRL_DECK_CTL_BEAT_JUMP_FORWARD,
    CTRL_DECK_CTL_PAD_MODE_HOT_CUE,
    CTRL_DECK_CTL_PAD_MODE_BEAT_LOOP,
    CTRL_DECK_CTL_PAD_MODE_BEAT_JUMP,
    CTRL_DECK_CTL_PAD_MODE_KEY_SHIFT,
    CTRL_DECK_CTL_PAD_ACTION,
    CTRL_DECK_CTL_PAD_MODE_KEYBOARD,
    CTRL_DECK_CTL_PAD_MODE_PAD_FX1,
    CTRL_DECK_CTL_PAD_MODE_PAD_FX2,
    CTRL_DECK_CTL_PAD_MODE_SAMPLER,
    CTRL_DECK_CTL_JOG_SEARCH,
    CTRL_DECK_CTL_JOG_SEARCH_TOUCH,
    CTRL_DECK_CTL_EXT_ACTION,
} ctrl_deck_control_t;

#define CTRL_NS_DECK1   0x10
#define CTRL_NS_DECK2   0x30
#define CTRL_NS_MIXER   0x50
#define CTRL_NS_BROWSER 0x60
#define CTRL_NS_SYSTEM  0x70

typedef enum {
    CTRL_PAD_MODE_HOT_CUE = 0,
    CTRL_PAD_MODE_BEAT_LOOP = 1,
    CTRL_PAD_MODE_BEAT_JUMP = 2,
    CTRL_PAD_MODE_KEY_SHIFT = 3,
    CTRL_PAD_MODE_KEYBOARD = 4,
    CTRL_PAD_MODE_PAD_FX1 = 5,
    CTRL_PAD_MODE_PAD_FX2 = 6,
    CTRL_PAD_MODE_SAMPLER = 7,
} ctrl_pad_mode_t;

#define CTRL_PAD_ACTION_VALUE(mode, pad, shifted, pressed) \
    ((int16_t)((((pad) & 0x07)      ) | \
               (((mode) & 0x07) << 3) | \
               ((shifted) ? 0x40 : 0x00) | \
               ((pressed) ? 0x80 : 0x00)))
#define CTRL_PAD_ACTION_PAD(value)     ((uint8_t)((value) & 0x07))
#define CTRL_PAD_ACTION_MODE(value)    ((uint8_t)(((value) >> 3) & 0x07))
#define CTRL_PAD_ACTION_SHIFTED(value) (((value) & 0x40) != 0)
#define CTRL_PAD_ACTION_PRESSED(value) (((value) & 0x80) != 0)

typedef enum {
    CTRL_DECK_EXT_ACTION_CENSOR = 0,
    CTRL_DECK_EXT_ACTION_SYNC_MASTER,
    CTRL_DECK_EXT_ACTION_RELOOP_STOP,
    CTRL_DECK_EXT_ACTION_LOOP_ADJUST_IN,
    CTRL_DECK_EXT_ACTION_LOOP_ADJUST_OUT,
    CTRL_DECK_EXT_ACTION_QUANTIZE,
} ctrl_deck_ext_action_t;

#define CTRL_DECK_EXT_VALUE(action, pressed) \
    ((int16_t)(((action) & 0x7F) | ((pressed) ? 0x80 : 0x00)))
#define CTRL_DECK_EXT_ACTION(value) ((uint8_t)((value) & 0x7F))
#define CTRL_DECK_EXT_PRESSED(value) (((value) & 0x80) != 0)

#define CTRL_ID_FLX4_CONNECTION (CTRL_NS_SYSTEM | 0x00)
#define CTRL_ID_SMART_CFX       (CTRL_NS_SYSTEM | 0x01)
#define CTRL_ID_SMART_FADER     (CTRL_NS_SYSTEM | 0x02)
#define CTRL_ID_BEAT_FX_SELECT_NEXT (CTRL_NS_SYSTEM | 0x03)
#define CTRL_ID_BEAT_FX_SELECT_PREV (CTRL_NS_SYSTEM | 0x04)
#define CTRL_ID_BEAT_FX_BEAT_DEC    (CTRL_NS_SYSTEM | 0x05)
#define CTRL_ID_BEAT_FX_BEAT_INC    (CTRL_NS_SYSTEM | 0x06)
#define CTRL_ID_BEAT_FX_TARGET      (CTRL_NS_SYSTEM | 0x07)
#define CTRL_ID_BEAT_FX_DEPTH       (CTRL_NS_SYSTEM | 0x08)
#define CTRL_ID_BEAT_FX_ON          (CTRL_NS_SYSTEM | 0x09)
#define CTRL_ID_BEAT_FX_CLEAR       (CTRL_NS_SYSTEM | 0x0A)
#define CTRL_ID_MASTER_VOLUME       (CTRL_NS_SYSTEM | 0x0B)
#define CTRL_ID_MASTER_CUE          (CTRL_NS_SYSTEM | 0x0C)
/*
 * Global (deck-less) semantic IDs. The system namespace CTRL_NS_SYSTEM = 0x70
 * spans 0x70..0x7F, so offsets 0x0D..0x0F below are its final three slots.
 * Once 0x7F is used the namespace is full; any further global IDs live as flat
 * values at 0x80 and above, outside every namespace. 0x80..0x82 are left
 * reserved as headroom, so 0x83..0x85 are the current overflow allocations.
 * Keep this block byte-for-byte identical on the S3 and P4 headers -- the
 * control_link_protocol host test asserts the two sides agree.
 */
#define CTRL_ID_HEADPHONE_LEVEL     0x7D  /* CTRL_NS_SYSTEM | 0x0D */
#define CTRL_ID_SMART_CFX_SHIFT     0x7E  /* CTRL_NS_SYSTEM | 0x0E */
#define CTRL_ID_SMART_FADER_SHIFT   0x7F  /* CTRL_NS_SYSTEM | 0x0F -- namespace full */
/* Flat global overflow region (no namespace); 0x80..0x82 reserved for future use. */
#define CTRL_ID_BEAT_FX_BEAT_DEC_SHIFT 0x83
#define CTRL_ID_BEAT_FX_BEAT_INC_SHIFT 0x84
#define CTRL_ID_S3_DEBUG_AP            0x85

typedef enum {
    CTRL_S3_DEBUG_AP_OFF = 0,
    CTRL_S3_DEBUG_AP_STARTING = 1,
    CTRL_S3_DEBUG_AP_ON = 2,
    CTRL_S3_DEBUG_AP_ERROR = 3,
} ctrl_s3_debug_ap_status_t;

/*
 * ── 0xA6 bulk frame layer ────────────────────────────────────────────────────
 * Variable-length frames on the same UART for payloads that do not fit the
 * 7-byte 0xA5 event frame (controller descriptor reports now; the profile
 * transfer protocol later). Layout:
 *
 *   [0] 0xA6 start   [1] type   [2] seq   [3] len (0..128)
 *   [4..4+len)  payload
 *   [4+len]     crc_lo   [5+len] crc_hi
 *
 * CRC16-CCITT (poly 0x1021, init 0xFFFF, no reflection) over bytes [1..4+len).
 * Keep this block byte-for-byte identical on the S3 and P4 headers -- the
 * control_link_protocol host test asserts the two sides agree.
 */
#define CTRL_BULK_FRAME_START 0xA6
#define CTRL_BULK_MAX_PAYLOAD 128
#define CTRL_BULK_HEADER_LEN  4
#define CTRL_BULK_CRC_LEN     2
#define CTRL_BULK_MAX_FRAME   (CTRL_BULK_HEADER_LEN + CTRL_BULK_MAX_PAYLOAD + CTRL_BULK_CRC_LEN)

#define CTRL_BULK_TYPE_CONTROLLER_DESCRIPTOR 0x01
#define CTRL_BULK_TYPE_PROFILE_BEGIN         0x02  /* P4->S3 */
#define CTRL_BULK_TYPE_PROFILE_CHUNK         0x03  /* P4->S3 */
#define CTRL_BULK_TYPE_PROFILE_END           0x04  /* P4->S3 */
#define CTRL_BULK_TYPE_PROFILE_ACK           0x05  /* S3->P4 */
#define CTRL_BULK_TYPE_PROFILE_NACK          0x06  /* S3->P4 */
#define CTRL_BULK_TYPE_PROFILE_ACTIVATE      0x07  /* P4->S3 */
#define CTRL_BULK_TYPE_PROFILE_STATUS        0x08  /* S3->P4 */
#define CTRL_BULK_TYPE_PROFILE_CLEAR         0x09  /* P4->S3 */

/* CONTROLLER_DESCRIPTOR payload: vid u16 LE, pid u16 LE, caps u16 LE,
 * product string (CTRL_DESC_PRODUCT_MAX bytes, NUL-padded). */
#define CTRL_DESC_PRODUCT_MAX 32
#define CTRL_DESC_PAYLOAD_LEN (6 + CTRL_DESC_PRODUCT_MAX)
#define CTRL_DESC_CAP_MIDI_IN   0x0001
#define CTRL_DESC_CAP_MIDI_OUT  0x0002
#define CTRL_DESC_CAP_USB_AUDIO 0x0004

typedef struct {
    uint16_t vid;
    uint16_t pid;
    uint16_t caps;
    char product[CTRL_DESC_PRODUCT_MAX + 1];
} ctrl_descriptor_report_t;

/* Profile transfer payloads (P4 sends a compiled S3CP profile to the S3):
 *   BEGIN    total_size u32, transfer crc32 u32, vid u16, pid u16   (12 B)
 *   CHUNK    offset u32, data[1..CTRL_PROFILE_CHUNK_MAX]
 *   END      (empty)
 *   ACTIVATE (empty)      CLEAR (empty)
 *   ACK      acked_type u8
 *   NACK     nacked_type u8, reason u8 (ctrl_profile_nack_t)
 *   STATUS   state u8 (ctrl_profile_state_t), vid u16, pid u16      (5 B)
 * The transfer crc32 is IEEE 802.3 over the whole blob; the S3CP file also
 * carries its own internal crc32, so a transfer is double-checked. */
#define CTRL_PROFILE_BEGIN_LEN  12
#define CTRL_PROFILE_CHUNK_HDR  4
#define CTRL_PROFILE_CHUNK_MAX  (CTRL_BULK_MAX_PAYLOAD - CTRL_PROFILE_CHUNK_HDR)
#define CTRL_PROFILE_STATUS_LEN 5

typedef enum {
    CTRL_PROFILE_NACK_NONE = 0,
    CTRL_PROFILE_NACK_SIZE,    /* total_size zero or beyond receiver capacity */
    CTRL_PROFILE_NACK_STATE,   /* frame arrived in the wrong transfer state */
    CTRL_PROFILE_NACK_OFFSET,  /* chunk offset/len out of range */
    CTRL_PROFILE_NACK_CRC,     /* END transfer crc32 mismatch */
    CTRL_PROFILE_NACK_PARSE,   /* ACTIVATE: reassembled bytes are not a profile */
} ctrl_profile_nack_t;

typedef enum {
    CTRL_PROFILE_STATE_IDLE = 0,
    CTRL_PROFILE_STATE_RECEIVING,
    CTRL_PROFILE_STATE_STORED,
    CTRL_PROFILE_STATE_ACTIVE,
    CTRL_PROFILE_STATE_ERROR,
} ctrl_profile_state_t;

/* Incremental 0xA6 frame parser (pure; host-tested). Feed RX bytes one at a
 * time: returns the full frame length when a valid frame completed (frame
 * bytes in .buf), 0 while in progress or idle, -1 on CRC/format error (the
 * parser resets itself). Bytes that are not part of a bulk frame are only
 * consumed when the parser is mid-frame. */
typedef struct {
    uint8_t buf[CTRL_BULK_MAX_FRAME];
    int pos;
    int total_len;
} ctrl_bulk_parser_t;

void ctrl_bulk_parser_reset(ctrl_bulk_parser_t *p);
int ctrl_bulk_parser_feed(ctrl_bulk_parser_t *p, uint8_t b);

/* Frame builders: serialise into `out` (cap bytes), return frame length or 0. */
size_t ctrl_bulk_build_descriptor_frame(uint8_t *out, size_t cap, uint8_t seq,
                                        const ctrl_descriptor_report_t *rep);
size_t ctrl_bulk_build_profile_begin(uint8_t *out, size_t cap, uint8_t seq,
                                     uint32_t total_size, uint32_t crc32,
                                     uint16_t vid, uint16_t pid);
size_t ctrl_bulk_build_profile_chunk(uint8_t *out, size_t cap, uint8_t seq,
                                     uint32_t offset, const uint8_t *data,
                                     size_t len);
size_t ctrl_bulk_build_profile_simple(uint8_t *out, size_t cap, uint8_t seq,
                                      uint8_t type);
size_t ctrl_bulk_build_profile_ack(uint8_t *out, size_t cap, uint8_t seq,
                                   uint8_t acked_type);
size_t ctrl_bulk_build_profile_nack(uint8_t *out, size_t cap, uint8_t seq,
                                    uint8_t nacked_type, uint8_t reason);
size_t ctrl_bulk_build_profile_status(uint8_t *out, size_t cap, uint8_t seq,
                                      uint8_t state, uint16_t vid, uint16_t pid);

/* Frame decoders operate on a parser-validated frame (type + length checked). */
bool ctrl_bulk_decode_descriptor(const uint8_t *frame, size_t frame_len,
                                 ctrl_descriptor_report_t *rep);
bool ctrl_bulk_decode_profile_begin(const uint8_t *frame, size_t frame_len,
                                    uint32_t *total_size, uint32_t *crc32,
                                    uint16_t *vid, uint16_t *pid);
bool ctrl_bulk_decode_profile_chunk(const uint8_t *frame, size_t frame_len,
                                    uint32_t *offset, const uint8_t **data,
                                    size_t *len);
bool ctrl_bulk_decode_profile_ack(const uint8_t *frame, size_t frame_len,
                                  uint8_t *acked_type);
bool ctrl_bulk_decode_profile_nack(const uint8_t *frame, size_t frame_len,
                                   uint8_t *nacked_type, uint8_t *reason);
bool ctrl_bulk_decode_profile_status(const uint8_t *frame, size_t frame_len,
                                     uint8_t *state, uint16_t *vid, uint16_t *pid);

/* Profile transfer receiver (S3 role): reassembles PROFILE_BEGIN/CHUNK/END
 * into a caller-provided buffer and verifies the transfer crc32. Chunks must
 * arrive in order and contiguous. Pure; host-tested. Each step returns a
 * ctrl_profile_nack_t (0 == CTRL_PROFILE_NACK_NONE == ok); on any non-zero the
 * receiver moves to CTRL_PROFILE_STATE_ERROR and a fresh BEGIN restarts it. */
typedef struct {
    uint8_t *buf;
    size_t cap;
    uint8_t state;      /* ctrl_profile_state_t */
    uint16_t vid;
    uint16_t pid;
    uint32_t total;
    uint32_t crc;
    uint32_t received;
} cp_xfer_rx_t;

uint32_t cp_xfer_crc32(const uint8_t *data, size_t len);
void cp_xfer_rx_init(cp_xfer_rx_t *rx, uint8_t *buf, size_t cap);
uint8_t cp_xfer_rx_begin(cp_xfer_rx_t *rx, uint32_t total_size, uint32_t crc32,
                         uint16_t vid, uint16_t pid);
uint8_t cp_xfer_rx_chunk(cp_xfer_rx_t *rx, uint32_t offset,
                         const uint8_t *data, size_t len);
uint8_t cp_xfer_rx_end(cp_xfer_rx_t *rx);

typedef enum {
    CTRL_BEAT_FX_TARGET_CH1 = 0,
    CTRL_BEAT_FX_TARGET_CH2 = 1,
    CTRL_BEAT_FX_TARGET_BOTH = 2,
} ctrl_beat_fx_target_t;

typedef enum {
    CTRL_FLX4_DISCONNECTED = 0,
    CTRL_FLX4_CONNECTED = 1,
} ctrl_flx4_connection_t;

#define CTRL_ID_DECK1_PLAY                  (CTRL_NS_DECK1 + CTRL_DECK_CTL_PLAY)
#define CTRL_ID_DECK1_CUE                   (CTRL_NS_DECK1 + CTRL_DECK_CTL_CUE)
#define CTRL_ID_DECK1_JOG_SCRATCH           (CTRL_NS_DECK1 + CTRL_DECK_CTL_JOG_SCRATCH)
#define CTRL_ID_DECK1_JOG_BEND              (CTRL_NS_DECK1 + CTRL_DECK_CTL_JOG_BEND)
#define CTRL_ID_DECK1_JOG_TOUCH             (CTRL_NS_DECK1 + CTRL_DECK_CTL_JOG_TOUCH)
#define CTRL_ID_DECK1_TEMPO                 (CTRL_NS_DECK1 + CTRL_DECK_CTL_TEMPO)
#define CTRL_ID_DECK1_SHIFT                 (CTRL_NS_DECK1 + CTRL_DECK_CTL_SHIFT)
#define CTRL_ID_DECK1_TO_START              (CTRL_NS_DECK1 + CTRL_DECK_CTL_TO_START)
#define CTRL_ID_DECK1_SYNC                  (CTRL_NS_DECK1 + CTRL_DECK_CTL_SYNC)
#define CTRL_ID_DECK1_TEMPO_RANGE           (CTRL_NS_DECK1 + CTRL_DECK_CTL_TEMPO_RANGE)
#define CTRL_ID_DECK1_LOOP_IN               (CTRL_NS_DECK1 + CTRL_DECK_CTL_LOOP_IN)
#define CTRL_ID_DECK1_LOOP_OUT              (CTRL_NS_DECK1 + CTRL_DECK_CTL_LOOP_OUT)
#define CTRL_ID_DECK1_RELOOP_EXIT           (CTRL_NS_DECK1 + CTRL_DECK_CTL_RELOOP_EXIT)
#define CTRL_ID_DECK1_LOOP_HALVE            (CTRL_NS_DECK1 + CTRL_DECK_CTL_LOOP_HALVE)
#define CTRL_ID_DECK1_LOOP_DOUBLE           (CTRL_NS_DECK1 + CTRL_DECK_CTL_LOOP_DOUBLE)
#define CTRL_ID_DECK1_BEAT_JUMP_BACK        (CTRL_NS_DECK1 + CTRL_DECK_CTL_BEAT_JUMP_BACK)
#define CTRL_ID_DECK1_BEAT_JUMP_FORWARD     (CTRL_NS_DECK1 + CTRL_DECK_CTL_BEAT_JUMP_FORWARD)
#define CTRL_ID_DECK1_PAD_MODE_HOT_CUE      (CTRL_NS_DECK1 + CTRL_DECK_CTL_PAD_MODE_HOT_CUE)
#define CTRL_ID_DECK1_PAD_MODE_BEAT_LOOP    (CTRL_NS_DECK1 + CTRL_DECK_CTL_PAD_MODE_BEAT_LOOP)
#define CTRL_ID_DECK1_PAD_MODE_BEAT_JUMP    (CTRL_NS_DECK1 + CTRL_DECK_CTL_PAD_MODE_BEAT_JUMP)
#define CTRL_ID_DECK1_PAD_MODE_KEY_SHIFT    (CTRL_NS_DECK1 + CTRL_DECK_CTL_PAD_MODE_KEY_SHIFT)
#define CTRL_ID_DECK1_PAD_ACTION            (CTRL_NS_DECK1 + CTRL_DECK_CTL_PAD_ACTION)
#define CTRL_ID_DECK1_PAD_MODE_KEYBOARD     (CTRL_NS_DECK1 + CTRL_DECK_CTL_PAD_MODE_KEYBOARD)
#define CTRL_ID_DECK1_PAD_MODE_PAD_FX1      (CTRL_NS_DECK1 + CTRL_DECK_CTL_PAD_MODE_PAD_FX1)
#define CTRL_ID_DECK1_PAD_MODE_PAD_FX2      (CTRL_NS_DECK1 + CTRL_DECK_CTL_PAD_MODE_PAD_FX2)
#define CTRL_ID_DECK1_PAD_MODE_SAMPLER      (CTRL_NS_DECK1 + CTRL_DECK_CTL_PAD_MODE_SAMPLER)
#define CTRL_ID_DECK1_JOG_SEARCH            (CTRL_NS_DECK1 + CTRL_DECK_CTL_JOG_SEARCH)
#define CTRL_ID_DECK1_JOG_SEARCH_TOUCH      (CTRL_NS_DECK1 + CTRL_DECK_CTL_JOG_SEARCH_TOUCH)
#define CTRL_ID_DECK1_EXT_ACTION            (CTRL_NS_DECK1 + CTRL_DECK_CTL_EXT_ACTION)

#define CTRL_ID_DECK2_PLAY                  (CTRL_NS_DECK2 + CTRL_DECK_CTL_PLAY)
#define CTRL_ID_DECK2_CUE                   (CTRL_NS_DECK2 + CTRL_DECK_CTL_CUE)
#define CTRL_ID_DECK2_JOG_SCRATCH           (CTRL_NS_DECK2 + CTRL_DECK_CTL_JOG_SCRATCH)
#define CTRL_ID_DECK2_JOG_BEND              (CTRL_NS_DECK2 + CTRL_DECK_CTL_JOG_BEND)
#define CTRL_ID_DECK2_JOG_TOUCH             (CTRL_NS_DECK2 + CTRL_DECK_CTL_JOG_TOUCH)
#define CTRL_ID_DECK2_TEMPO                 (CTRL_NS_DECK2 + CTRL_DECK_CTL_TEMPO)
#define CTRL_ID_DECK2_SHIFT                 (CTRL_NS_DECK2 + CTRL_DECK_CTL_SHIFT)
#define CTRL_ID_DECK2_TO_START              (CTRL_NS_DECK2 + CTRL_DECK_CTL_TO_START)
#define CTRL_ID_DECK2_SYNC                  (CTRL_NS_DECK2 + CTRL_DECK_CTL_SYNC)
#define CTRL_ID_DECK2_TEMPO_RANGE           (CTRL_NS_DECK2 + CTRL_DECK_CTL_TEMPO_RANGE)
#define CTRL_ID_DECK2_LOOP_IN               (CTRL_NS_DECK2 + CTRL_DECK_CTL_LOOP_IN)
#define CTRL_ID_DECK2_LOOP_OUT              (CTRL_NS_DECK2 + CTRL_DECK_CTL_LOOP_OUT)
#define CTRL_ID_DECK2_RELOOP_EXIT           (CTRL_NS_DECK2 + CTRL_DECK_CTL_RELOOP_EXIT)
#define CTRL_ID_DECK2_LOOP_HALVE            (CTRL_NS_DECK2 + CTRL_DECK_CTL_LOOP_HALVE)
#define CTRL_ID_DECK2_LOOP_DOUBLE           (CTRL_NS_DECK2 + CTRL_DECK_CTL_LOOP_DOUBLE)
#define CTRL_ID_DECK2_BEAT_JUMP_BACK        (CTRL_NS_DECK2 + CTRL_DECK_CTL_BEAT_JUMP_BACK)
#define CTRL_ID_DECK2_BEAT_JUMP_FORWARD     (CTRL_NS_DECK2 + CTRL_DECK_CTL_BEAT_JUMP_FORWARD)
#define CTRL_ID_DECK2_PAD_MODE_HOT_CUE      (CTRL_NS_DECK2 + CTRL_DECK_CTL_PAD_MODE_HOT_CUE)
#define CTRL_ID_DECK2_PAD_MODE_BEAT_LOOP    (CTRL_NS_DECK2 + CTRL_DECK_CTL_PAD_MODE_BEAT_LOOP)
#define CTRL_ID_DECK2_PAD_MODE_BEAT_JUMP    (CTRL_NS_DECK2 + CTRL_DECK_CTL_PAD_MODE_BEAT_JUMP)
#define CTRL_ID_DECK2_PAD_MODE_KEY_SHIFT    (CTRL_NS_DECK2 + CTRL_DECK_CTL_PAD_MODE_KEY_SHIFT)
#define CTRL_ID_DECK2_PAD_ACTION            (CTRL_NS_DECK2 + CTRL_DECK_CTL_PAD_ACTION)
#define CTRL_ID_DECK2_PAD_MODE_KEYBOARD     (CTRL_NS_DECK2 + CTRL_DECK_CTL_PAD_MODE_KEYBOARD)
#define CTRL_ID_DECK2_PAD_MODE_PAD_FX1      (CTRL_NS_DECK2 + CTRL_DECK_CTL_PAD_MODE_PAD_FX1)
#define CTRL_ID_DECK2_PAD_MODE_PAD_FX2      (CTRL_NS_DECK2 + CTRL_DECK_CTL_PAD_MODE_PAD_FX2)
#define CTRL_ID_DECK2_PAD_MODE_SAMPLER      (CTRL_NS_DECK2 + CTRL_DECK_CTL_PAD_MODE_SAMPLER)
#define CTRL_ID_DECK2_JOG_SEARCH            (CTRL_NS_DECK2 + CTRL_DECK_CTL_JOG_SEARCH)
#define CTRL_ID_DECK2_JOG_SEARCH_TOUCH      (CTRL_NS_DECK2 + CTRL_DECK_CTL_JOG_SEARCH_TOUCH)
#define CTRL_ID_DECK2_EXT_ACTION            (CTRL_NS_DECK2 + CTRL_DECK_CTL_EXT_ACTION)

#define CTRL_ID_CH1_VOLUME        (CTRL_NS_MIXER | 0x00)
#define CTRL_ID_CH2_VOLUME        (CTRL_NS_MIXER | 0x01)
#define CTRL_ID_CROSSFADER        (CTRL_NS_MIXER | 0x02)
#define CTRL_ID_DECK1_PFL         (CTRL_NS_MIXER | 0x03)
#define CTRL_ID_DECK2_PFL         (CTRL_NS_MIXER | 0x04)
#define CTRL_ID_CH1_TRIM          (CTRL_NS_MIXER | 0x05)
#define CTRL_ID_CH2_TRIM          (CTRL_NS_MIXER | 0x06)
#define CTRL_ID_CH1_EQ_HIGH       (CTRL_NS_MIXER | 0x07)
#define CTRL_ID_CH2_EQ_HIGH       (CTRL_NS_MIXER | 0x08)
#define CTRL_ID_CH1_EQ_MID        (CTRL_NS_MIXER | 0x09)
#define CTRL_ID_CH2_EQ_MID        (CTRL_NS_MIXER | 0x0A)
#define CTRL_ID_CH1_EQ_LOW        (CTRL_NS_MIXER | 0x0B)
#define CTRL_ID_CH2_EQ_LOW        (CTRL_NS_MIXER | 0x0C)
#define CTRL_ID_CH1_FILTER        (CTRL_NS_MIXER | 0x0D)
#define CTRL_ID_CH2_FILTER        (CTRL_NS_MIXER | 0x0E)
#define CTRL_ID_HEADPHONE_MIX     (CTRL_NS_MIXER | 0x0F)

#define CTRL_ID_BROWSE_DELTA      (CTRL_NS_BROWSER | 0x00)
#define CTRL_ID_LOAD_DECK1        (CTRL_NS_BROWSER | 0x01)
#define CTRL_ID_LOAD_DECK2        (CTRL_NS_BROWSER | 0x02)
#define CTRL_ID_BROWSE_PRESS      (CTRL_NS_BROWSER | 0x03)
#define CTRL_ID_BROWSE_SHIFT_DELTA (CTRL_NS_BROWSER | 0x04)
#define CTRL_ID_BROWSE_SHIFT_PRESS (CTRL_NS_BROWSER | 0x05)
#define CTRL_ID_SHIFT_LOAD_DECK1  (CTRL_NS_BROWSER | 0x06)
#define CTRL_ID_SHIFT_LOAD_DECK2  (CTRL_NS_BROWSER | 0x07)

static inline bool control_link_id_is_deck(uint8_t id)
{
    return (id >= CTRL_NS_DECK1 && id < CTRL_NS_DECK1 + 0x20) ||
           (id >= CTRL_NS_DECK2 && id < CTRL_NS_DECK2 + 0x20);
}

static inline uint8_t control_link_id_deck(uint8_t id)
{
    if (id >= CTRL_NS_DECK1 && id < CTRL_NS_DECK1 + 0x20) return CTRL_DECK_1;
    if (id >= CTRL_NS_DECK2 && id < CTRL_NS_DECK2 + 0x20) return CTRL_DECK_2;
    return CTRL_DECK_NONE;
}

static inline uint8_t control_link_id_control(uint8_t id)
{
    if (id >= CTRL_NS_DECK1 && id < CTRL_NS_DECK1 + 0x20) return (uint8_t)(id - CTRL_NS_DECK1);
    if (id >= CTRL_NS_DECK2 && id < CTRL_NS_DECK2 + 0x20) return (uint8_t)(id - CTRL_NS_DECK2);
    return id;
}

static inline bool control_link_id_is_deck_jog(uint8_t id)
{
    if (!control_link_id_is_deck(id)) return false;
    uint8_t ctl = control_link_id_control(id);
    return ctl == CTRL_DECK_CTL_JOG_SCRATCH ||
           ctl == CTRL_DECK_CTL_JOG_BEND ||
           ctl == CTRL_DECK_CTL_JOG_SEARCH;
}

// ─── Parsed event (what deck_core receives) ───────────────────────────────────

typedef enum {
    CTRL_EV_BUTTON = 0,
    CTRL_EV_JOG,
    CTRL_EV_BROWSE,
    CTRL_EV_PITCH,
    CTRL_EV_HEARTBEAT,
    CTRL_EV_STATE,
} ctrl_event_type_t;

typedef struct {
    ctrl_event_type_t type;
    uint8_t           id;
    int16_t           value;
    uint8_t           seq;
    uint8_t           deck;     // CTRL_DECK_* for DDJ-FLX4 deck IDs
    uint8_t           control;  // low-nibble semantic control for deck IDs
} ctrl_event_t;

// ─── Public API ───────────────────────────────────────────────────────────────

// Initialise UART and start RX task.
// Parsed events are pushed onto ctrl_event_queue (created by deck_core_init).
esp_err_t control_link_init(QueueHandle_t ctrl_event_queue);

// Send LED command to S3. Thread-safe.
void control_link_send_led(led_id_t led, uint8_t state);  // state: 0 off / 1 on / 2 blink
void control_link_send_led_deck(led_id_t led, uint8_t state, uint8_t deck);

// Send a deck-less state/control command to S3. Thread-safe.
void control_link_send_state(uint8_t id, int16_t value);

// Register a callback invoked (from the RX task) whenever the S3 reports a
// connected controller descriptor over the 0xA6 bulk layer.
typedef void (*control_link_descriptor_cb_t)(const ctrl_descriptor_report_t *rep);
void control_link_set_descriptor_report_cb(control_link_descriptor_cb_t cb);

// Profile transfer (P4 -> S3): send BEGIN/CHUNK and the empty END/ACTIVATE/
// CLEAR frames over the 0xA6 bulk layer. Thread-safe; call from the sender
// task, not the RX task.
esp_err_t control_link_send_profile_begin(uint32_t total_size, uint32_t crc32,
                                          uint16_t vid, uint16_t pid);
esp_err_t control_link_send_profile_chunk(uint32_t offset, const uint8_t *data,
                                          size_t len);
esp_err_t control_link_send_profile_simple(uint8_t type);

// Register a callback invoked (from the RX task) when the S3 replies to a
// profile transfer frame. `ack` is true for PROFILE_ACK, false for
// PROFILE_NACK; `ref_type` is the frame type being acked/nacked and `reason`
// is a ctrl_profile_nack_t (0 on ACK).
typedef void (*control_link_profile_reply_cb_t)(bool ack, uint8_t ref_type,
                                                uint8_t reason);
void control_link_set_profile_reply_cb(control_link_profile_reply_cb_t cb);
