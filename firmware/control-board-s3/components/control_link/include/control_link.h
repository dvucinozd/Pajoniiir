#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "panel_io.h"

#ifndef CONFIG_CONTROL_LINK_UART_TX_GPIO
#define CONFIG_CONTROL_LINK_UART_TX_GPIO 5
#endif

#ifndef CONFIG_CONTROL_LINK_UART_RX_GPIO
#define CONFIG_CONTROL_LINK_UART_RX_GPIO 6
#endif

#define CONTROL_LINK_UART_TX_GPIO CONFIG_CONTROL_LINK_UART_TX_GPIO
#define CONTROL_LINK_UART_RX_GPIO CONFIG_CONTROL_LINK_UART_RX_GPIO

// Inter-board UART protocol constants.
//
// Frame layout (7 bytes):
//   [0] 0xA5        start byte
//   [1] type        see CTRL_TYPE_* below
//   [2] id          button/encoder/led id
//   [3] val_lo      value LSB
//   [4] val_hi      value MSB
//   [5] seq         rolling sequence 0–255
//   [6] checksum    XOR of bytes [1]..[5]

#define CTRL_FRAME_LEN   7
#define CTRL_FRAME_START 0xA5

// S3 → P4 event types
#define CTRL_TYPE_BUTTON   0x01  // id=button_id, val=0/1
#define CTRL_TYPE_ENCODER  0x02  // id=0 jog / 1 browse, val=signed delta
#define CTRL_TYPE_PITCH    0x03  // id=0, val=0–16383
#define CTRL_TYPE_HEARTBEAT 0x04 // id=0, val=uptime seconds

// P4 → S3 command types
#define CTRL_TYPE_LED      0x81  // id=led_id, val=0 off / 1 on / 2 blink
#define CTRL_TYPE_STATE    0x82  // id=state_id, val=state value (reserved)

// DDJ-FLX4 deck-aware semantic IDs. Values must match the P4 control_link
// header; the wire frame stays unchanged.
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

#define LED_VU_METER 5
#define LED_PAD_MODE_HOT_CUE 6
#define LED_PAD_MODE_KEYBOARD 7
#define LED_PAD_MODE_PAD_FX1 8
#define LED_PAD_MODE_PAD_FX2 9
#define LED_PAD_MODE_BEAT_JUMP 10
#define LED_PAD_MODE_BEAT_LOOP 11
#define LED_PAD_MODE_SAMPLER 12
#define LED_PAD_MODE_KEY_SHIFT 13
#define LED_SYNC 14
#define LED_LOOP_IN 15
#define LED_LOOP_OUT 16
#define LED_BEAT_LOOP_PAD_1 17
#define LED_BEAT_LOOP_PAD_2 18
#define LED_BEAT_LOOP_PAD_3 19
#define LED_BEAT_LOOP_PAD_4 20
#define LED_BEAT_LOOP_PAD_5 21
#define LED_BEAT_LOOP_PAD_6 22
#define LED_BEAT_LOOP_PAD_7 23
#define LED_BEAT_LOOP_PAD_8 24
#define LED_PAD_FX1_PAD_1 25
#define LED_PAD_FX1_PAD_2 26
#define LED_PAD_FX1_PAD_3 27
#define LED_PAD_FX1_PAD_4 28
#define LED_PAD_FX1_PAD_5 29
#define LED_PAD_FX1_PAD_6 30
#define LED_PAD_FX1_PAD_7 31
#define LED_PAD_FX1_PAD_8 32
#define LED_PAD_FX2_PAD_1 33
#define LED_PAD_FX2_PAD_2 34
#define LED_PAD_FX2_PAD_3 35
#define LED_PAD_FX2_PAD_4 36
#define LED_PAD_FX2_PAD_5 37
#define LED_PAD_FX2_PAD_6 38
#define LED_PAD_FX2_PAD_7 39
#define LED_PAD_FX2_PAD_8 40
#define LED_SMART_CFX 41
#define LED_SMART_FADER 42
#define LED_BEAT_FX_ON 43
#define LED_HOT_CUE_PAD_1 44
#define LED_HOT_CUE_PAD_2 45
#define LED_HOT_CUE_PAD_3 46
#define LED_HOT_CUE_PAD_4 47
#define LED_HOT_CUE_PAD_5 48
#define LED_HOT_CUE_PAD_6 49
#define LED_HOT_CUE_PAD_7 50
#define LED_HOT_CUE_PAD_8 51
#define LED_MASTER_CUE 52
/* LED_CENSOR is state-driven: the P4 publishes it through the periodic FLX4 LED
 * snapshot (flx4_led_snapshot.c) from deck_state_t.censor_active. */
#define LED_CENSOR 53
/* The LEDs below are output-only capability: their MIDI note mapping exists in
 * flx4_led_midi.c and is packet-tested, but no P4 deck handler drives them yet.
 * They are reserved for the momentary press/release feedback described in the
 * gap-closure plan and are intentionally NOT part of the state snapshot. */
#define LED_CUE_SHIFT 54
#define LED_LOOP_ADJUST_IN 55
#define LED_LOOP_ADJUST_OUT 56
#define LED_TRACK_LOAD_DECK1 57
#define LED_TRACK_LOAD_DECK2 58
/* Beat Jump pad LEDs are P4-owned FLX4 pad feedback. They intentionally live
 * outside the legacy FLX4 snapshot table and are published by P4 deck_core. */
#define LED_BEAT_JUMP_PAD_1 59
#define LED_BEAT_JUMP_PAD_2 60
#define LED_BEAT_JUMP_PAD_3 61
#define LED_BEAT_JUMP_PAD_4 62
#define LED_BEAT_JUMP_PAD_5 63
#define LED_BEAT_JUMP_PAD_6 64
#define LED_BEAT_JUMP_PAD_7 65
#define LED_BEAT_JUMP_PAD_8 66
#define LED_BEAT_JUMP_SHIFT_HELPER_7 67
#define LED_BEAT_JUMP_SHIFT_HELPER_8 68
#define LED_REMOTE_COUNT 69

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
#define CTRL_BULK_TYPE_FIRMWARE_REPORT       0x0A  /* S3->P4 */

#define CTRL_FW_VERSION_MAX 32
#define CTRL_FW_REPORT_LEN  (2 + CTRL_FW_VERSION_MAX)

typedef enum {
    CTRL_FW_SLOT_UNKNOWN = 0,
    CTRL_FW_SLOT_OTA_0 = 1,
    CTRL_FW_SLOT_OTA_1 = 2,
    CTRL_FW_SLOT_FACTORY = 3,
} ctrl_firmware_slot_t;

typedef enum {
    CTRL_FW_STATE_UNKNOWN = 0,
    CTRL_FW_STATE_NEW = 1,
    CTRL_FW_STATE_PENDING_VERIFY = 2,
    CTRL_FW_STATE_VALID = 3,
    CTRL_FW_STATE_INVALID = 4,
    CTRL_FW_STATE_ABORTED = 5,
} ctrl_firmware_state_t;

typedef struct {
    uint8_t slot;
    uint8_t state;
    char version[CTRL_FW_VERSION_MAX + 1];
} ctrl_firmware_report_t;

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
size_t ctrl_bulk_build_firmware_report(uint8_t *out, size_t cap, uint8_t seq,
                                       const ctrl_firmware_report_t *rep);

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
bool ctrl_bulk_decode_firmware_report(const uint8_t *frame, size_t frame_len,
                                      ctrl_firmware_report_t *rep);

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

// Initialise UART1 and start RX task.
// panel_event_queue: the queue returned by panel_io_init().
// Received LED/state commands from the P4 are applied to panel LEDs directly.
esp_err_t control_link_init(QueueHandle_t panel_event_queue);

// Send a deck-aware DDJ-FLX4 semantic event to P4 over the existing frame.
// Safe to call from any task.
esp_err_t control_link_send_semantic(uint8_t type, uint8_t id, int16_t value);

// Serialise one panel event and transmit to ESP32-P4 over UART.
// Safe to call from any task.
void control_link_send_event(const panel_event_t *ev);

// Send a CTRL_TYPE_HEARTBEAT frame with the current uptime in seconds.
// Call periodically (e.g. every 5 s) so the P4 can detect S3 disconnects.
// Safe to call from any task.
void control_link_send_heartbeat(void);

// Serialise a controller descriptor report into a 0xA6 bulk frame.
// Returns the frame length written, or 0 when `out` is too small.
// Pure helper (no UART); host tests exercise it directly.
size_t ctrl_bulk_build_descriptor_frame(uint8_t *out, size_t cap, uint8_t seq,
                                        const ctrl_descriptor_report_t *rep);

// Send a controller descriptor report to the P4. Safe to call from any task.
esp_err_t control_link_send_descriptor_report(const ctrl_descriptor_report_t *rep);

// Send the running S3 firmware slot/state/version to P4 as a 0xA6 report.
esp_err_t control_link_send_firmware_report(const ctrl_firmware_report_t *rep);

// Called (from the RX task) when the P4 requests activation of a fully
// received profile. `blob`/`len` are the validated S3CP bytes; return true to
// accept (ACK) or false to reject (NACK PARSE). A CLEAR request calls this with
// blob == NULL / len == 0. When no callback is registered, ACTIVATE is ACKed
// and the stored blob is left available via control_link_get_stored_profile().
typedef bool (*control_link_profile_activate_cb_t)(const uint8_t *blob,
                                                   size_t len,
                                                   uint16_t vid, uint16_t pid);
void control_link_set_profile_activate_cb(control_link_profile_activate_cb_t cb);

// Access the last fully received + CRC-validated profile blob (valid after an
// END ACK). Returns NULL when none is stored. `len`/`vid`/`pid` may be NULL.
const uint8_t *control_link_get_stored_profile(size_t *len, uint16_t *vid,
                                               uint16_t *pid);
