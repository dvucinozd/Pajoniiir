#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/* Transport-neutral semantic event kinds produced by the local USB runtime. */
#define CTRL_TYPE_BUTTON  0x01
#define CTRL_TYPE_ENCODER 0x02
#define CTRL_TYPE_PITCH   0x03
#define CTRL_TYPE_STATE   0x82

// ─── Legacy deck button IDs ──────────────────────────────────────────────────

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

// ─── Controller LED IDs ──────────────────────────────────────────────────────

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
    /* Direct deck feedback outside the fixed periodic snapshot. CUE_SHIFT is a
     * momentary acknowledgement; LOOP_ADJUST_IN/OUT show the active jog-edit
     * mode and are restored explicitly on controller reconnect. */
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
    CTRL_DECK_CTL_LOOP_SIZE,
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
    CTRL_DECK_EXT_ACTION_SYNC_OFF,
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
 * reserved as headroom, so 0x83..0x89 are the current overflow allocations.
 * 0x80..0x82 remain reserved as headroom.
 */
#define CTRL_ID_HEADPHONE_LEVEL     0x7D  /* CTRL_NS_SYSTEM | 0x0D */
#define CTRL_ID_SMART_CFX_SHIFT     0x7E  /* CTRL_NS_SYSTEM | 0x0E */
#define CTRL_ID_SMART_FADER_SHIFT   0x7F  /* CTRL_NS_SYSTEM | 0x0F -- namespace full */
/* Flat global overflow region (no namespace). */
#define CTRL_ID_BEAT_FX_BEAT_DEC_SHIFT 0x83
#define CTRL_ID_BEAT_FX_BEAT_INC_SHIFT 0x84
#define CTRL_DESC_CAP_MIDI_IN   0x0001
#define CTRL_DESC_CAP_MIDI_OUT  0x0002
#define CTRL_DESC_CAP_USB_AUDIO 0x0004

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
#define CTRL_ID_DECK1_LOOP_SIZE             (CTRL_NS_DECK1 + CTRL_DECK_CTL_LOOP_SIZE)

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
#define CTRL_ID_DECK2_LOOP_SIZE             (CTRL_NS_DECK2 + CTRL_DECK_CTL_LOOP_SIZE)

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
           ctl == CTRL_DECK_CTL_JOG_SEARCH ||
           ctl == CTRL_DECK_CTL_LOOP_SIZE;
}

// ─── Parsed event (what deck_core receives) ───────────────────────────────────

typedef enum {
    CTRL_EV_BUTTON = 0,
    CTRL_EV_JOG,
    CTRL_EV_BROWSE,
    CTRL_EV_PITCH,
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

// Bind the P4-local controller producer to deck_core's event queue.
esp_err_t control_link_init(QueueHandle_t ctrl_event_queue);

// Send an LED command to the directly attached controller.
void control_link_send_led(led_id_t led, uint8_t state);  // state: 0 off / 1 on / 2 blink
void control_link_send_led_deck(led_id_t led, uint8_t state, uint8_t deck);

typedef esp_err_t (*control_link_led_sink_fn_t)(uint8_t led, uint8_t state,
                                                uint8_t deck, void *user_ctx);
void control_link_set_led_sink(control_link_led_sink_fn_t sink, void *user_ctx);

// Inject one transport-neutral semantic event from the direct USB runtime.
esp_err_t control_link_inject_semantic(uint8_t type, uint8_t id, int16_t value);
