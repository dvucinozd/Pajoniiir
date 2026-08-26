#include "deck_core.h"
#include "deck_loaded_track_store.h"
#include "control_link.h"
#include "flx4_led_snapshot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "audio_engine.h"
#include "beat_jump.h"
#include "hot_cue_store.h"
#include "rekordbox_anlz.h"
#if !defined(DECK_CORE_PC_TEST)
#include "esp_timer.h"
#include "sdkconfig.h"   /* CONFIG_AUDIO_SCRATCH_ENABLED (undefined -> Phase 1) */
#endif
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "deck";

#define CTRL_QUEUE_LEN  32
#define PITCH_CENTER    8192
#define SEARCH_STEP_MS  5000
#define JOG_SEARCH_STEP_MS 1000
#define DEFAULT_TEMPO_RANGE_PERCENT 10u
#define BEAT_SYNC_MAX_PERCENT 20u
#define BROWSE_SHIFT_LIBRARY_MULTIPLIER 10
#define BROWSE_SHIFT_OVERVIEW_MULTIPLIER 4
#define CENSOR_REPEAT_BACK_MS 1000u
#define DECK_TASK_STACK_BYTES 8192u
#define DECK_UI_COMMAND_QUEUE_LEN 16
#define DECK_UI_COMMANDS_PER_FRAME 8u
#define DECK_CORE_TEST_UI_COMMAND_QUEUE_LEN 32u
#define DECK_CORE_INTERNAL_RESET_ID 0xFEu
#define DECK_CORE_RESET_TIMEOUT_MS 2000u
#define BEAT_FX_ECHO_FALLBACK_BPM 120.0f
#define BEAT_FX_ECHO_MIN_BPM 40.0f
#define BEAT_FX_ECHO_MAX_BPM 300.0f
#define BEAT_FX_ECHO_MAX_DELAY_MS 1000u
#define BEAT_FX_FLANGER_MIN_PERIOD_MS 100u
#define BEAT_FX_FLANGER_MAX_PERIOD_MS 8000u

extern bool ui_is_library_active(void) __attribute__((weak));
extern bool ui_is_overview_active(void) __attribute__((weak));
extern esp_err_t ui_show_library(void) __attribute__((weak));
extern esp_err_t ui_toggle_library_view(void) __attribute__((weak));
extern esp_err_t ui_library_select_delta(int delta) __attribute__((weak));
extern esp_err_t ui_overview_zoom_delta(int delta) __attribute__((weak));
extern esp_err_t ui_library_load_selected(void) __attribute__((weak));
extern esp_err_t ui_library_load_selected_for_deck(uint8_t deck) __attribute__((weak));

static QueueHandle_t    s_queue;
static QueueHandle_t    s_ui_command_queue;
static SemaphoreHandle_t s_mutex;
static deck_state_t     s_decks[DECK_CORE_DECK_COUNT];
static deck_state_t     s_published_decks[DECK_CORE_DECK_COUNT];
static deck_core_beat_fx_state_t s_published_beat_fx;
static uint32_t         s_published_heartbeat_tick;
static uint32_t         s_snapshot_seq;
static bool             s_snapshot_writer;
static deck_loaded_track_store_t s_loaded_tracks;
static SemaphoreHandle_t s_reset_done_sem;
static flx4_led_publisher_t s_flx4_led_publisher;
static deck_core_beat_fx_state_t s_beat_fx;
static bool              s_track_load_led_valid[DECK_CORE_DECK_COUNT];
static uint8_t           s_track_load_led_state[DECK_CORE_DECK_COUNT];
static bool              s_loaded_hot_cue_mask_valid[DECK_CORE_DECK_COUNT];
static uint8_t           s_loaded_hot_cue_mask[DECK_CORE_DECK_COUNT];
static bool              s_snapshot_suppress_inactive_pads;
static bool              s_beat_jump_pad_led_valid[DECK_CORE_DECK_COUNT][8];
static uint8_t           s_beat_jump_pad_led_state[DECK_CORE_DECK_COUNT][8];
static bool              s_deck_shift_held[DECK_CORE_DECK_COUNT];
/* Jog platter touch (vinyl mode Phase 1): true while the platter top is held.
 * Touching during playback enters platter-hold (audio silenced + position frozen)
 * and jogs then scrub the position; s_jog_hold_active[] remembers a hold was
 * entered so release resumes forward playback. */
static bool              s_jog_touched[DECK_CORE_DECK_COUNT];
static bool              s_jog_hold_active[DECK_CORE_DECK_COUNT];
static bool              s_jog_scratch_active[DECK_CORE_DECK_COUNT];
static bool              s_beat_jump_shift_helper_led_valid[DECK_CORE_DECK_COUNT][2];
static uint8_t           s_beat_jump_shift_helper_led_state[DECK_CORE_DECK_COUNT][2];
static uint32_t          s_drop_count;
static TickType_t        s_last_drop_warn;
static TickType_t        s_last_heartbeat_tick;
static bool              s_flx4_connection_state_valid;
static bool              s_flx4_connected;
static uint8_t           s_sync_master_deck = CTRL_DECK_NONE;
static deck_core_s3_debug_ap_status_cb_t s_s3_debug_ap_status_cb;
static deck_core_s3_debug_ap_token_cb_t s_s3_debug_ap_token_cb;
static uint16_t          s_s3_debug_token_hi;
static bool              s_s3_debug_token_hi_valid;

typedef enum {
    DECK_UI_CMD_LOAD_SELECTED,
    DECK_UI_CMD_LIBRARY_SELECT_DELTA_IF_ACTIVE,
    DECK_UI_CMD_BROWSE_DELTA,
    DECK_UI_CMD_TOGGLE_LIBRARY_VIEW,
    DECK_UI_CMD_SHOW_LIBRARY,
} deck_ui_command_kind_t;

typedef struct {
    deck_ui_command_kind_t kind;
    uint8_t deck;
    uint8_t id;
    int16_t value;
} deck_ui_command_t;

#if defined(DECK_CORE_PC_TEST)
static deck_ui_command_t s_test_ui_commands[DECK_CORE_TEST_UI_COMMAND_QUEUE_LEN];
static size_t s_test_ui_command_count;
#endif
#if !defined(DECK_CORE_PC_TEST)
static TaskHandle_t s_deck_task;
static TaskHandle_t s_vu_task;
/* Tracks whether the VU meters were last driven non-idle, so a single "all
 * zero" frame is emitted on the play->idle transition and then sending stops. */
static bool s_vu_meters_active;
#endif

static void deck_core_cleanup_init_failure(void)
{
#if !defined(DECK_CORE_PC_TEST)
    if (s_vu_task) {
        vTaskDelete(s_vu_task);
        s_vu_task = NULL;
    }
    if (s_deck_task) {
        vTaskDelete(s_deck_task);
        s_deck_task = NULL;
    }
#endif
    if (s_ui_command_queue) {
        vQueueDelete(s_ui_command_queue);
        s_ui_command_queue = NULL;
    }
    if (s_queue) {
        vQueueDelete(s_queue);
        s_queue = NULL;
    }
    if (s_mutex) {
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
    }
}
#if defined(DECK_CORE_PC_TEST)
static uint16_t          s_deferred_mixer_last[256];
static bool              s_deferred_mixer_seen[256];
#endif

typedef struct {
    bool pending_in;
    uint32_t pending_start_ms;
    bool last_valid;
    uint32_t last_start_ms;
    uint32_t last_end_ms;
} deck_loop_shadow_t;

static deck_loop_shadow_t s_loop_shadow[DECK_CORE_DECK_COUNT];
static deck_loop_shadow_t s_published_loop_shadow[DECK_CORE_DECK_COUNT];

typedef struct {
    bool active;
    bool previous_active;
    uint32_t previous_start_ms;
    uint32_t previous_end_ms;
} deck_shifted_loop_roll_t;

static deck_shifted_loop_roll_t s_shifted_loop_roll[DECK_CORE_DECK_COUNT];

static void publish_state_snapshot(void)
{
    while (__atomic_exchange_n(&s_snapshot_writer, true, __ATOMIC_ACQ_REL)) {
        taskYIELD();
    }
    (void)__atomic_add_fetch(&s_snapshot_seq, 1u, __ATOMIC_RELEASE); /* odd */
    memcpy(s_published_decks, s_decks, sizeof(s_published_decks));
    memcpy(s_published_loop_shadow, s_loop_shadow, sizeof(s_published_loop_shadow));
    s_published_beat_fx = s_beat_fx;
    s_published_heartbeat_tick = (uint32_t)s_last_heartbeat_tick;
    (void)__atomic_add_fetch(&s_snapshot_seq, 1u, __ATOMIC_RELEASE); /* even */
    __atomic_store_n(&s_snapshot_writer, false, __ATOMIC_RELEASE);
}

static void copy_state_snapshot(uint8_t deck,
                                deck_state_t *out_deck,
                                deck_loop_shadow_t *out_loop,
                                deck_core_beat_fx_state_t *out_fx,
                                uint32_t *out_heartbeat)
{
    /* Retry until a copy is bracketed by the same even sequence number.
     *
     * Written as an unconditional loop with an explicit exit rather than a
     * do-while: with `continue` in a do-while the odd-sequence path jumps
     * straight to the condition, which then reads `after` before anything has
     * assigned it. That is an indeterminate value, and if it happened to come
     * out even and equal to `before` the function returned having copied
     * nothing - handing the caller a zeroed deck state that reads as "no track,
     * not playing" while the deck is mid-set. */
    for (;;) {
        const uint32_t before = __atomic_load_n(&s_snapshot_seq, __ATOMIC_ACQUIRE);
        if (before & 1u) {
            continue;   /* writer mid-update; re-read the sequence */
        }
        if (out_deck) *out_deck = s_published_decks[deck];
        if (out_loop) *out_loop = s_published_loop_shadow[deck];
        if (out_fx) *out_fx = s_published_beat_fx;
        if (out_heartbeat) *out_heartbeat = s_published_heartbeat_tick;
        const uint32_t after = __atomic_load_n(&s_snapshot_seq, __ATOMIC_ACQUIRE);
        if (after == before) {
            return;     /* no write began or ended while we copied */
        }
    }
}

typedef struct {
    bool active;
    uint8_t mode;
    uint8_t pad;
} deck_pad_fx_led_state_t;

static deck_pad_fx_led_state_t s_pad_fx_led[DECK_CORE_DECK_COUNT];

typedef struct {
    bool active;
    uint8_t pad;
} deck_beat_loop_led_state_t;

static deck_beat_loop_led_state_t s_beat_loop_led[DECK_CORE_DECK_COUNT];

typedef struct {
    bool active;
    bool was_playing;
    uint32_t origin_ms;
    TickType_t press_tick;
} deck_censor_shadow_t;

static deck_censor_shadow_t s_censor_shadow[DECK_CORE_DECK_COUNT];

/* Hot-cue exists-mask cache: publish_flx4_led_snapshot() needs the mask on
 * every publish, and reading it from NVS each time puts flash reads on the
 * input-handling path. All cue writes go through this file, so the cache is
 * refreshed on save and only misses once per loaded track. */
static uint32_t s_hot_cue_mask_cache_key[DECK_CORE_DECK_COUNT];
static uint8_t  s_hot_cue_mask_cache_value[DECK_CORE_DECK_COUNT];

static void hot_cue_mask_cache_invalidate(uint8_t deck)
{
    if (deck >= DECK_CORE_DECK_COUNT) {
        return;
    }
    s_hot_cue_mask_cache_key[deck] = 0;
    s_hot_cue_mask_cache_value[deck] = 0;
}

static void hot_cue_mask_cache_store(uint8_t deck, uint32_t track_key, uint8_t mask)
{
    if (deck < DECK_CORE_DECK_COUNT) {
        s_hot_cue_mask_cache_key[deck] = track_key;
        s_hot_cue_mask_cache_value[deck] = mask;
    }
    /* Keep the other deck coherent when both decks hold the same track. */
    for (uint8_t d = 0; d < DECK_CORE_DECK_COUNT; d++) {
        if (d != deck && s_hot_cue_mask_cache_key[d] == track_key) {
            s_hot_cue_mask_cache_value[d] = mask;
        }
    }
}

#define DECK_CORE_DEFERRED_MIXER_LOG_STEP 2048u

static void publish_flx4_led_snapshot(bool force);
static void publish_loaded_track_hot_cue_leds(uint8_t deck);
static void apply_deck_pitch(uint8_t deck, deck_state_t *state);
static bool apply_beat_sync(uint8_t deck, deck_state_t *state);
static uint8_t beat_sync_reference_deck(uint8_t deck);
static void set_sync_master(uint8_t deck, deck_state_t *state);
static float deck_effective_bpm(uint8_t deck, const deck_state_t *state);

static void init_deck_state(deck_state_t *state)
{
    memset(state, 0, sizeof(*state));
    state->pitch = PITCH_CENTER;
    state->pitch_centipercent = 0;
    state->tempo_range_percent = DEFAULT_TEMPO_RANGE_PERCENT;
    state->pad_mode = CTRL_PAD_MODE_HOT_CUE;
}

static void init_beat_fx_state(void)
{
    s_beat_fx.effect = DECK_CORE_BEAT_FX_FILTER;
    s_beat_fx.beat = DECK_CORE_BEAT_FX_BEAT_1;
    s_beat_fx.target = CTRL_BEAT_FX_TARGET_BOTH;
    s_beat_fx.depth = 64;
    s_beat_fx.enabled = false;
}

static audio_engine_beat_fx_target_t beat_fx_audio_target(ctrl_beat_fx_target_t target)
{
    switch (target) {
    case CTRL_BEAT_FX_TARGET_CH1:
        return AUDIO_ENGINE_BEAT_FX_TARGET_CH1;
    case CTRL_BEAT_FX_TARGET_CH2:
        return AUDIO_ENGINE_BEAT_FX_TARGET_CH2;
    case CTRL_BEAT_FX_TARGET_BOTH:
    default:
        return AUDIO_ENGINE_BEAT_FX_TARGET_BOTH;
    }
}

static bool beat_fx_beat_ratio(deck_core_beat_fx_beat_t beat,
                               uint16_t *out_numerator,
                               uint16_t *out_denominator)
{
    switch (beat) {
    case DECK_CORE_BEAT_FX_BEAT_1_4:
        *out_numerator = 1u;
        *out_denominator = 4u;
        return true;
    case DECK_CORE_BEAT_FX_BEAT_1_2:
        *out_numerator = 1u;
        *out_denominator = 2u;
        return true;
    case DECK_CORE_BEAT_FX_BEAT_1:
        *out_numerator = 1u;
        *out_denominator = 1u;
        return true;
    case DECK_CORE_BEAT_FX_BEAT_2:
        *out_numerator = 2u;
        *out_denominator = 1u;
        return true;
    case DECK_CORE_BEAT_FX_BEAT_4:
        *out_numerator = 4u;
        *out_denominator = 1u;
        return true;
    default:
        return false;
    }
}

static float beat_fx_target_bpm(ctrl_beat_fx_target_t target)
{
    uint8_t deck = CTRL_DECK_1;
    if (target == CTRL_BEAT_FX_TARGET_CH2) {
        deck = CTRL_DECK_2;
    }

    float bpm = deck_effective_bpm(deck, &s_decks[deck]);
    if (bpm < BEAT_FX_ECHO_MIN_BPM || bpm > BEAT_FX_ECHO_MAX_BPM) {
        return BEAT_FX_ECHO_FALLBACK_BPM;
    }
    return bpm;
}

static uint32_t beat_fx_delay_ms(deck_core_beat_fx_beat_t beat,
                                 ctrl_beat_fx_target_t target)
{
    uint16_t numerator = 1u;
    uint16_t denominator = 1u;
    if (!beat_fx_beat_ratio(beat, &numerator, &denominator) || denominator == 0u) {
        return 500u;
    }

    float bpm = beat_fx_target_bpm(target);
    float delay = (60000.0f * (float)numerator) / (bpm * (float)denominator);
    uint32_t delay_ms = (uint32_t)(delay + 0.5f);
    if (delay_ms < 1u) {
        delay_ms = 1u;
    }
    if (delay_ms > BEAT_FX_ECHO_MAX_DELAY_MS) {
        delay_ms = BEAT_FX_ECHO_MAX_DELAY_MS;
    }
    return delay_ms;
}

/* Flanger LFO period follows the beat selector directly (1/4 beat = fast
 * jet, 4 beats = slow sweep); unlike the echo it is not bound by the 1 s
 * delay-buffer cap. */
static uint32_t beat_fx_flanger_period_ms(deck_core_beat_fx_beat_t beat,
                                          ctrl_beat_fx_target_t target)
{
    uint16_t numerator = 1u;
    uint16_t denominator = 1u;
    if (!beat_fx_beat_ratio(beat, &numerator, &denominator) || denominator == 0u) {
        return 500u;
    }

    float bpm = beat_fx_target_bpm(target);
    float period = (60000.0f * (float)numerator) / (bpm * (float)denominator);
    uint32_t period_ms = (uint32_t)(period + 0.5f);
    if (period_ms < BEAT_FX_FLANGER_MIN_PERIOD_MS) {
        period_ms = BEAT_FX_FLANGER_MIN_PERIOD_MS;
    }
    if (period_ms > BEAT_FX_FLANGER_MAX_PERIOD_MS) {
        period_ms = BEAT_FX_FLANGER_MAX_PERIOD_MS;
    }
    return period_ms;
}

static deck_core_beat_fx_effect_t beat_fx_next_effect(
    deck_core_beat_fx_effect_t effect)
{
    switch (effect) {
    case DECK_CORE_BEAT_FX_FILTER:
        return DECK_CORE_BEAT_FX_ECHO;
    case DECK_CORE_BEAT_FX_ECHO:
        return DECK_CORE_BEAT_FX_FLANGER;
    case DECK_CORE_BEAT_FX_FLANGER:
        return DECK_CORE_BEAT_FX_DELAY;
    case DECK_CORE_BEAT_FX_DELAY:
    case DECK_CORE_BEAT_FX_NONE:
    default:
        return DECK_CORE_BEAT_FX_FILTER;
    }
}

static deck_core_beat_fx_effect_t beat_fx_previous_effect(
    deck_core_beat_fx_effect_t effect)
{
    switch (effect) {
    case DECK_CORE_BEAT_FX_FILTER:
        return DECK_CORE_BEAT_FX_DELAY;
    case DECK_CORE_BEAT_FX_DELAY:
        return DECK_CORE_BEAT_FX_FLANGER;
    case DECK_CORE_BEAT_FX_FLANGER:
        return DECK_CORE_BEAT_FX_ECHO;
    case DECK_CORE_BEAT_FX_ECHO:
        return DECK_CORE_BEAT_FX_FILTER;
    case DECK_CORE_BEAT_FX_NONE:
    default:
        return DECK_CORE_BEAT_FX_DELAY;
    }
}

static void sync_beat_fx_audio_state(void)
{
    audio_engine_beat_fx_target_t target = beat_fx_audio_target(s_beat_fx.target);
    bool filter_enabled = s_beat_fx.enabled &&
                          s_beat_fx.effect == DECK_CORE_BEAT_FX_FILTER;
    bool echo_enabled = s_beat_fx.enabled &&
                        s_beat_fx.effect == DECK_CORE_BEAT_FX_ECHO;
    bool delay_enabled = s_beat_fx.enabled &&
                         s_beat_fx.effect == DECK_CORE_BEAT_FX_DELAY;
    bool flanger_enabled = s_beat_fx.enabled &&
                           s_beat_fx.effect == DECK_CORE_BEAT_FX_FLANGER;

    audio_engine_set_beat_fx_filter(target,
                                    s_beat_fx.depth,
                                    filter_enabled);
    uint32_t delay_ms = beat_fx_delay_ms(s_beat_fx.beat, s_beat_fx.target);
    /* ECHO and DELAY share one per-deck stereo delay line. Publish exactly
     * one time-effect command so a second setter cannot overwrite the first. */
    if (s_beat_fx.effect == DECK_CORE_BEAT_FX_DELAY) {
        audio_engine_set_beat_fx_delay(target,
                                       s_beat_fx.depth,
                                       delay_ms,
                                       delay_enabled);
    } else {
        audio_engine_set_beat_fx_echo(target,
                                      s_beat_fx.depth,
                                      delay_ms,
                                      echo_enabled);
    }
    audio_engine_set_beat_fx_flanger(target,
                                     s_beat_fx.depth,
                                     beat_fx_flanger_period_ms(s_beat_fx.beat, s_beat_fx.target),
                                     flanger_enabled);
}

static uint8_t normalize_deck(uint8_t deck)
{
    return deck < DECK_CORE_DECK_COUNT ? deck : DECK_CORE_COMPAT_DECK;
}

static uint8_t deck_index_for_event(const ctrl_event_t *ev)
{
    if (ev && (ev->id == CTRL_ID_LOAD_DECK1 ||
               ev->id == CTRL_ID_SHIFT_LOAD_DECK1)) {
        return CTRL_DECK_1;
    }
    if (ev && (ev->id == CTRL_ID_LOAD_DECK2 ||
               ev->id == CTRL_ID_SHIFT_LOAD_DECK2)) {
        return CTRL_DECK_2;
    }
    if (ev && control_link_id_is_deck(ev->id)) {
        return control_link_id_deck(ev->id);
    }
    if (ev && ev->deck < DECK_CORE_DECK_COUNT) {
        return ev->deck;
    }
    return DECK_CORE_COMPAT_DECK;
}

static bool deck_uses_audio_engine(uint8_t deck)
{
    return deck < DECK_CORE_DECK_COUNT;
}

static int16_t tempo_centipercent_from_raw(int16_t raw, uint16_t range_percent)
{
    int32_t clamped = raw;
    if (clamped < 0) {
        clamped = 0;
    } else if (clamped > 16383) {
        clamped = 16383;
    }
    int32_t centi_range = (int32_t)range_percent * 100;
    return (int16_t)(((int32_t)PITCH_CENTER - clamped) * centi_range / PITCH_CENTER);
}

static uint16_t next_tempo_range_percent(uint16_t current)
{
    switch (current) {
    case 6:
        return 10;
    case 10:
        return 16;
    default:
        return 6;
    }
}

static uint16_t deck_base_bpm(uint8_t deck)
{
    deck_loaded_track_summary_t loaded = {0};
    if (deck_loaded_track_store_get(&s_loaded_tracks, deck, &loaded) &&
        loaded.valid) {
        if (loaded.bpm_x100 > 0u) {
            return (uint16_t)((loaded.bpm_x100 + 50u) / 100u);
        }
        if (loaded.bpm > 0u) {
            return loaded.bpm;
        }
    }
    return 120u;
}

static uint32_t deck_base_bpm_x100(uint8_t deck)
{
    deck_loaded_track_summary_t loaded = {0};
    if (deck_loaded_track_store_get(&s_loaded_tracks, deck, &loaded) &&
        loaded.valid &&
        loaded.bpm_x100 > 0u) {
        return loaded.bpm_x100;
    }
    return (uint32_t)deck_base_bpm(deck) * 100u;
}

static float deck_effective_bpm(uint8_t deck, const deck_state_t *state)
{
    float bpm = (float)deck_base_bpm_x100(deck) / 100.0f;
    return bpm * (1.0f + deck_core_pitch_percent(state) / 100.0f);
}

static int16_t clamp_centipercent_to_range(int32_t centipercent, uint16_t range_percent)
{
    int32_t max = (int32_t)range_percent * 100;
    if (centipercent > max) {
        return (int16_t)max;
    }
    if (centipercent < -max) {
        return (int16_t)-max;
    }
    return (int16_t)centipercent;
}

static int16_t centipercent_for_bpm_match(uint8_t deck, uint8_t reference_deck)
{
    uint32_t target_base_x100 = deck_base_bpm_x100(deck);
    if (target_base_x100 == 0) {
        target_base_x100 = 12000u;
    }
    float reference_bpm = deck_effective_bpm(reference_deck, &s_decks[reference_deck]);
    float target_base_bpm = (float)target_base_x100 / 100.0f;
    float target_percent = ((reference_bpm / target_base_bpm) - 1.0f) * 100.0f;
    int32_t centipercent = (int32_t)(target_percent * 100.0f +
                                    (target_percent >= 0.0f ? 0.5f : -0.5f));
    return clamp_centipercent_to_range(centipercent, BEAT_SYNC_MAX_PERCENT);
}

static bool beat_sync_requires_clamp(uint8_t deck, uint8_t reference_deck, int16_t applied_centipercent)
{
    uint32_t target_base_x100 = deck_base_bpm_x100(deck);
    if (target_base_x100 == 0) {
        target_base_x100 = 12000u;
    }
    float reference_bpm = deck_effective_bpm(reference_deck, &s_decks[reference_deck]);
    float target_base_bpm = (float)target_base_x100 / 100.0f;
    float required_percent = ((reference_bpm / target_base_bpm) - 1.0f) * 100.0f;
    int32_t required_centipercent = (int32_t)(required_percent * 100.0f +
                                             (required_percent >= 0.0f ? 0.5f : -0.5f));
    return required_centipercent != applied_centipercent;
}

static float deck_synced_bpm_after_pitch(uint8_t deck, int16_t pitch_centipercent)
{
    float bpm = (float)deck_base_bpm_x100(deck) / 100.0f;
    return bpm * (1.0f + ((float)pitch_centipercent / 10000.0f));
}

static bool event_is_mixer_control(const ctrl_event_t *ev)
{
    return ev && (ev->id == CTRL_ID_CH1_VOLUME ||
                  ev->id == CTRL_ID_CH2_VOLUME ||
                  ev->id == CTRL_ID_CROSSFADER ||
                  ev->id == CTRL_ID_DECK1_PFL ||
                  ev->id == CTRL_ID_DECK2_PFL ||
                  ev->id == CTRL_ID_CH1_TRIM ||
                  ev->id == CTRL_ID_CH2_TRIM ||
                  ev->id == CTRL_ID_CH1_EQ_HIGH ||
                  ev->id == CTRL_ID_CH2_EQ_HIGH ||
                  ev->id == CTRL_ID_CH1_EQ_MID ||
                  ev->id == CTRL_ID_CH2_EQ_MID ||
                  ev->id == CTRL_ID_CH1_EQ_LOW ||
                  ev->id == CTRL_ID_CH2_EQ_LOW ||
                  ev->id == CTRL_ID_CH1_FILTER ||
                  ev->id == CTRL_ID_CH2_FILTER ||
                  ev->id == CTRL_ID_HEADPHONE_MIX ||
                  ev->id == CTRL_ID_MASTER_VOLUME);
}

#if defined(DECK_CORE_PC_TEST)
static bool is_deferred_mixer_control(uint8_t id)
{
    switch (id) {
    default:
        return false;
    }
}

static bool should_log_deferred_mixer_value(uint8_t id, uint16_t value)
{
    if (!is_deferred_mixer_control(id)) {
        return false;
    }

    if (!s_deferred_mixer_seen[id]) {
        s_deferred_mixer_seen[id] = true;
        s_deferred_mixer_last[id] = value;
        return true;
    }

    uint16_t last = s_deferred_mixer_last[id];
    uint16_t delta = value > last ? value - last : last - value;
    if (delta < DECK_CORE_DEFERRED_MIXER_LOG_STEP) {
        return false;
    }

    s_deferred_mixer_last[id] = value;
    return true;
}
#endif

static bool should_log_deferred_button(uint8_t id, int16_t value)
{
    if (id == CTRL_ID_BEAT_FX_SELECT_NEXT ||
        id == CTRL_ID_BEAT_FX_SELECT_PREV ||
        id == CTRL_ID_BEAT_FX_BEAT_DEC ||
        id == CTRL_ID_BEAT_FX_BEAT_INC ||
        id == CTRL_ID_BEAT_FX_BEAT_DEC_SHIFT ||
        id == CTRL_ID_BEAT_FX_BEAT_INC_SHIFT ||
        id == CTRL_ID_BEAT_FX_TARGET ||
        id == CTRL_ID_BEAT_FX_ON ||
        id == CTRL_ID_BEAT_FX_CLEAR) {
        return value != 0;
    }
    if (id == CTRL_ID_DECK1_PAD_ACTION || id == CTRL_ID_DECK2_PAD_ACTION) {
        return CTRL_PAD_ACTION_PRESSED(value);
    }
    return value != 0;
}

static uint32_t current_deck_position_ms(uint8_t deck, const deck_state_t *state)
{
    if (deck_uses_audio_engine(deck)) {
        return audio_engine_deck_position_ms(deck);
    }
    return state ? state->position_ms : 0u;
}

static uint32_t loaded_track_key_for_deck(uint8_t deck)
{
    deck_loaded_track_summary_t loaded = {0};
    if (deck >= DECK_CORE_DECK_COUNT ||
        !deck_loaded_track_store_get(&s_loaded_tracks, deck, &loaded) ||
        !loaded.valid) {
        return 0;
    }
    return loaded.track_key;
}

static uint8_t hot_cue_exists_mask_for_deck(uint8_t deck)
{
    uint32_t track_key = loaded_track_key_for_deck(deck);
    if (track_key == 0) {
        return 0;
    }
    if (deck < DECK_CORE_DECK_COUNT && s_hot_cue_mask_cache_key[deck] == track_key) {
        return s_hot_cue_mask_cache_value[deck];
    }

    hot_cue_store_blob_t blob = {0};
    uint8_t mask = 0;
    if (hot_cue_store_load(track_key, &blob) == ESP_OK) {
        mask = (uint8_t)(blob.valid_mask & 0xFFu);
    }
    hot_cue_mask_cache_store(deck, track_key, mask);
    return mask;
}

static void handle_hot_cue_pad_action(uint8_t deck, uint8_t pad, bool shifted, deck_state_t *state)
{
    if (deck >= DECK_CORE_DECK_COUNT || pad >= HOT_CUE_STORE_SLOT_COUNT || !state) {
        return;
    }

    uint32_t track_key = loaded_track_key_for_deck(deck);
    if (track_key == 0) {
        ESP_LOGW(TAG, "deck %u hot cue pad %u ignored: no loaded track key",
                 (unsigned)deck + 1,
                 (unsigned)pad + 1);
        return;
    }

    hot_cue_store_blob_t blob = {0};
    esp_err_t rc = hot_cue_store_load(track_key, &blob);
    if (rc == ESP_ERR_NOT_FOUND) {
        memset(&blob, 0, sizeof(blob));
    } else if (rc != ESP_OK) {
        ESP_LOGW(TAG, "deck %u hot cue load failed: %s",
                 (unsigned)deck + 1,
                 esp_err_to_name(rc));
        return;
    }

    uint32_t bit = (1u << pad);
    if (shifted) {
        if ((blob.valid_mask & bit) == 0) {
            ESP_LOGI(TAG, "deck %u hot cue %u clear ignored: empty",
                     (unsigned)deck + 1,
                     (unsigned)pad + 1);
            return;
        }
        blob.valid_mask &= ~bit;
        memset(&blob.slots[pad], 0, sizeof(blob.slots[pad]));
        rc = hot_cue_store_save(track_key, &blob);
        if (rc == ESP_OK) {
            hot_cue_mask_cache_store(deck, track_key, (uint8_t)(blob.valid_mask & 0xFFu));
            ESP_LOGI(TAG, "deck %u hot cue %u cleared",
                     (unsigned)deck + 1,
                     (unsigned)pad + 1);
            publish_flx4_led_snapshot(false);
        } else {
            ESP_LOGW(TAG, "deck %u hot cue %u clear failed: %s",
                     (unsigned)deck + 1,
                     (unsigned)pad + 1,
                     esp_err_to_name(rc));
        }
        return;
    }

    if ((blob.valid_mask & bit) != 0) {
        uint32_t pos_ms = blob.slots[pad].pos_ms;
        rc = audio_engine_deck_seek(deck, pos_ms);
        if (rc == ESP_OK) {
            state->position_ms = pos_ms;
            ESP_LOGI(TAG, "deck %u hot cue %u recall -> %lu ms",
                     (unsigned)deck + 1,
                     (unsigned)pad + 1,
                     (unsigned long)pos_ms);
        } else {
            ESP_LOGW(TAG, "deck %u hot cue %u recall failed: %s",
                     (unsigned)deck + 1,
                     (unsigned)pad + 1,
                     esp_err_to_name(rc));
        }
        return;
    }

    uint32_t pos_ms = current_deck_position_ms(deck, state);
    blob.valid_mask |= bit;
    blob.slots[pad] = (hot_cue_store_slot_t) {
        .pos_ms = pos_ms,
        .end_ms = 0,
        .type = HOT_CUE_STORE_TYPE_SINGLE,
    };
    rc = hot_cue_store_save(track_key, &blob);
    if (rc == ESP_OK) {
        hot_cue_mask_cache_store(deck, track_key, (uint8_t)(blob.valid_mask & 0xFFu));
        ESP_LOGI(TAG, "deck %u hot cue %u set -> %lu ms",
                 (unsigned)deck + 1,
                 (unsigned)pad + 1,
                 (unsigned long)pos_ms);
        publish_flx4_led_snapshot(false);
    } else {
        ESP_LOGW(TAG, "deck %u hot cue %u set failed: %s",
                 (unsigned)deck + 1,
                 (unsigned)pad + 1,
                 esp_err_to_name(rc));
    }
}

static const int s_beat_jump_pad_shifts[8] = {
    -32, -16, -8, -4, 4, 8, 16, 32,
};

static bool acquire_loaded_track_for_deck(
    uint8_t deck,
    deck_loaded_track_summary_t *summary,
    anlz_snapshot_t **snapshot)
{
    return deck_loaded_track_store_acquire(
        &s_loaded_tracks, deck, summary, snapshot);
}

static uint16_t loaded_bpm_for_deck(uint8_t deck)
{
    return deck_base_bpm(deck);
}

static void handle_beat_jump(uint8_t deck, int beat_shift, deck_state_t *state)
{
    if (deck >= DECK_CORE_DECK_COUNT || !state || beat_shift == 0) {
        return;
    }

    uint32_t position_ms = current_deck_position_ms(deck, state);
    deck_loaded_track_summary_t loaded = {0};
    anlz_snapshot_t *snapshot = NULL;
    const bool has_loaded = acquire_loaded_track_for_deck(
        deck, &loaded, &snapshot);
    const anlz_metadata_t *meta = anlz_snapshot_metadata(snapshot);
    uint16_t bpm = has_loaded && loaded.bpm > 0u
                       ? loaded.bpm
                       : loaded_bpm_for_deck(deck);
    const anlz_metadata_t *meta_ptr =
        has_loaded && loaded.has_anlz ? meta : NULL;
    uint32_t target_ms = beat_jump_calculate_target_ms(
        position_ms, bpm, beat_shift, meta_ptr);
    anlz_snapshot_release(snapshot);

    esp_err_t rc = audio_engine_deck_seek(deck, target_ms);
    if (rc == ESP_OK) {
        state->position_ms = target_ms;
        ESP_LOGI(TAG, "deck %u beat jump %+d -> %lu ms",
                 (unsigned)deck + 1,
                 beat_shift,
                 (unsigned long)target_ms);
    } else {
        ESP_LOGW(TAG, "deck %u beat jump %+d failed: %s",
                 (unsigned)deck + 1,
                 beat_shift,
                 esp_err_to_name(rc));
    }
}

static bool beat_jump_shift_for_pad(uint8_t pad, int *out_shift)
{
    if (!out_shift || pad >= 8) {
        return false;
    }
    *out_shift = s_beat_jump_pad_shifts[pad];
    return true;
}

typedef struct {
    uint16_t numerator;
    uint16_t denominator;
} beat_loop_length_t;

static const beat_loop_length_t s_beat_loop_pad_lengths[8] = {
    {1, 32}, {1, 16}, {1, 8}, {1, 4},
    {1, 2}, {1, 1}, {2, 1}, {4, 1},
};

static bool beat_loop_length_for_pad(uint8_t pad, beat_loop_length_t *out_length)
{
    if (!out_length || pad >= 8) {
        return false;
    }
    *out_length = s_beat_loop_pad_lengths[pad];
    return true;
}

static void remember_last_loop(uint8_t deck, uint32_t start_ms, uint32_t end_ms)
{
    if (deck >= DECK_CORE_DECK_COUNT || end_ms <= start_ms) {
        return;
    }
    s_loop_shadow[deck].last_valid = true;
    s_loop_shadow[deck].last_start_ms = start_ms;
    s_loop_shadow[deck].last_end_ms = end_ms;
}

static bool read_active_loop(uint8_t deck, bool *active, uint32_t *start_ms, uint32_t *end_ms)
{
    if (deck >= DECK_CORE_DECK_COUNT || !active || !start_ms || !end_ms) {
        return false;
    }
    return audio_engine_deck_get_loop_state(deck, active, start_ms, end_ms) == ESP_OK;
}

static void set_deck_loop(uint8_t deck, uint32_t start_ms, uint32_t end_ms)
{
    if (deck >= DECK_CORE_DECK_COUNT || end_ms <= start_ms) {
        return;
    }
    esp_err_t rc = audio_engine_deck_set_loop(deck, start_ms, end_ms);
    if (rc == ESP_OK) {
        remember_last_loop(deck, start_ms, end_ms);
        ESP_LOGI(TAG, "deck %u loop set %lu-%lu ms",
                 (unsigned)deck + 1,
                 (unsigned long)start_ms,
                 (unsigned long)end_ms);
        publish_flx4_led_snapshot(false);
    } else {
        ESP_LOGW(TAG, "deck %u loop set failed: %s",
                 (unsigned)deck + 1,
                 esp_err_to_name(rc));
    }
}

static uint32_t nearest_beat_ms(uint8_t deck, uint32_t position_ms)
{
    deck_loaded_track_summary_t loaded = {0};
    anlz_snapshot_t *snapshot = NULL;
    if (!acquire_loaded_track_for_deck(deck, &loaded, &snapshot)) {
        return position_ms;
    }
    const anlz_metadata_t *meta = anlz_snapshot_metadata(snapshot);
    if (!loaded.has_anlz || !meta || !meta->beats ||
        meta->beat_count == 0u) {
        anlz_snapshot_release(snapshot);
        return position_ms;
    }

    /* Beatgrid times are monotonically increasing, so binary-search the first
     * beat at/after position_ms and compare it with its predecessor — O(log n)
     * instead of scanning the whole grid on every quantized action. */
    uint16_t lo = 0;
    uint16_t hi = meta->beat_count;   /* [lo, hi): candidates >= position_ms */
    while (lo < hi) {
        uint16_t mid = (uint16_t)(lo + (hi - lo) / 2u);
        if (meta->beats[mid].time_ms < position_ms) {
            lo = (uint16_t)(mid + 1u);
        } else {
            hi = mid;
        }
    }

    if (lo == 0u) {
        uint32_t result = meta->beats[0].time_ms;
        anlz_snapshot_release(snapshot);
        return result;                               /* before the first beat */
    }
    if (lo >= meta->beat_count) {
        uint32_t result = meta->beats[meta->beat_count - 1u].time_ms;
        anlz_snapshot_release(snapshot);
        return result;                               /* past the last beat */
    }

    uint32_t after_ms = meta->beats[lo].time_ms;
    uint32_t before_ms = meta->beats[lo - 1u].time_ms;
    uint32_t after_delta = after_ms - position_ms;
    uint32_t before_delta = position_ms - before_ms;
    uint32_t result = before_delta <= after_delta ? before_ms : after_ms;
    anlz_snapshot_release(snapshot);
    return result;
}

static uint32_t quantized_deck_position_ms(uint8_t deck, const deck_state_t *state)
{
    uint32_t position_ms = current_deck_position_ms(deck, state);
    if (!state || !state->quantize_enabled) {
        return position_ms;
    }
    return nearest_beat_ms(deck, position_ms);
}

static void stop_and_forget_loop(uint8_t deck)
{
    if (deck >= DECK_CORE_DECK_COUNT) {
        return;
    }
    (void)audio_engine_deck_clear_loop(deck);
    memset(&s_loop_shadow[deck], 0, sizeof(s_loop_shadow[deck]));
    memset(&s_shifted_loop_roll[deck], 0, sizeof(s_shifted_loop_roll[deck]));
    memset(&s_beat_loop_led[deck], 0, sizeof(s_beat_loop_led[deck]));
    ESP_LOGI(TAG, "deck %u loop stop", (unsigned)deck + 1);
    publish_flx4_led_snapshot(false);
}

static void adjust_loop_boundary(uint8_t deck, bool adjust_in, deck_state_t *state)
{
    bool active = false;
    uint32_t start_ms = 0;
    uint32_t end_ms = 0;
    if (!read_active_loop(deck, &active, &start_ms, &end_ms) || !active) {
        return;
    }

    uint32_t position_ms = quantized_deck_position_ms(deck, state);
    if (adjust_in) {
        if (position_ms < end_ms) {
            set_deck_loop(deck, position_ms, end_ms);
        }
    } else if (position_ms > start_ms) {
        set_deck_loop(deck, start_ms, position_ms);
    }
}

/*
 * Censor approximation. Real Pioneer censor plays the track in reverse while
 * held and, on release, resumes exactly where the untouched timeline would be
 * (so beat alignment is preserved). We approximate that without a reverse
 * decoder: on press we seek back CENSOR_REPEAT_BACK_MS and keep playing
 * FORWARD, and on release we snap to the "real" position (origin + time held)
 * so sync is not lost. Consequences of the approximation: playback is forward,
 * not reversed; the CENSOR_REPEAT_BACK_MS window plays once and does not loop;
 * and if held longer than that window the audible content diverges from a true
 * censor even though the release position stays correct.
 */
static void handle_censor(uint8_t deck, bool pressed, deck_state_t *state)
{
    if (deck >= DECK_CORE_DECK_COUNT || !state) {
        return;
    }

    deck_censor_shadow_t *shadow = &s_censor_shadow[deck];
    if (pressed) {
        if (shadow->active) {
            return;
        }
        uint32_t origin = current_deck_position_ms(deck, state);
        uint32_t repeat = origin > CENSOR_REPEAT_BACK_MS ? origin - CENSOR_REPEAT_BACK_MS : 0u;
        shadow->active = true;
        shadow->was_playing = audio_engine_deck_is_playing(deck);
        shadow->origin_ms = origin;
        shadow->press_tick = xTaskGetTickCount();
        state->censor_active = true;
        if (audio_engine_deck_seek(deck, repeat) == ESP_OK) {
            state->position_ms = repeat;
        }
        ESP_LOGI(TAG, "deck %u censor press -> %lu ms",
                 (unsigned)deck + 1,
                 (unsigned long)repeat);
        publish_flx4_led_snapshot(false);
        return;
    }

    if (!shadow->active) {
        return;
    }

    uint32_t target = shadow->origin_ms;
    if (shadow->was_playing) {
        TickType_t elapsed_ticks = xTaskGetTickCount() - shadow->press_tick;
        uint32_t elapsed_ms = (uint32_t)(elapsed_ticks * portTICK_PERIOD_MS);
        target += elapsed_ms;
    }
    if (audio_engine_deck_seek(deck, target) == ESP_OK) {
        state->position_ms = target;
    }
    state->censor_active = false;
    memset(shadow, 0, sizeof(*shadow));
    ESP_LOGI(TAG, "deck %u censor release -> %lu ms",
             (unsigned)deck + 1,
             (unsigned long)target);
    publish_flx4_led_snapshot(false);
}

static void handle_beat_loop_pad_action(uint8_t deck, uint8_t pad, deck_state_t *state)
{
    if (deck >= DECK_CORE_DECK_COUNT || !state) {
        return;
    }

    beat_loop_length_t length = {0};
    if (!beat_loop_length_for_pad(pad, &length)) {
        return;
    }

    uint32_t start_ms = current_deck_position_ms(deck, state);
    deck_loaded_track_summary_t loaded = {0};
    anlz_snapshot_t *snapshot = NULL;
    const bool has_loaded = acquire_loaded_track_for_deck(
        deck, &loaded, &snapshot);
    const anlz_metadata_t *meta = anlz_snapshot_metadata(snapshot);
    uint32_t duration_ms = beat_loop_calculate_duration_ms(start_ms,
                                                           loaded_bpm_for_deck(deck),
                                                           length.numerator,
                                                           length.denominator,
                                                           has_loaded && loaded.has_anlz
                                                               ? meta
                                                               : NULL);
    anlz_snapshot_release(snapshot);
    if (duration_ms == 0 || start_ms > UINT32_MAX - duration_ms) {
        return;
    }
    s_beat_loop_led[deck].active = true;
    s_beat_loop_led[deck].pad = pad;
    set_deck_loop(deck, start_ms, start_ms + duration_ms);
}

static void handle_shifted_beat_loop_press(uint8_t deck, uint8_t pad, deck_state_t *state)
{
    if (deck >= DECK_CORE_DECK_COUNT || !state) {
        return;
    }

    bool active = false;
    uint32_t start_ms = 0;
    uint32_t end_ms = 0;
    if (!read_active_loop(deck, &active, &start_ms, &end_ms)) {
        return;
    }

    deck_shifted_loop_roll_t *roll = &s_shifted_loop_roll[deck];
    roll->active = true;
    roll->previous_active = active;
    roll->previous_start_ms = start_ms;
    roll->previous_end_ms = end_ms;

    handle_beat_loop_pad_action(deck, pad, state);
}

static void handle_shifted_beat_loop_release(uint8_t deck)
{
    if (deck >= DECK_CORE_DECK_COUNT) {
        return;
    }

    deck_shifted_loop_roll_t *roll = &s_shifted_loop_roll[deck];
    if (!roll->active) {
        return;
    }

    if (roll->previous_active && roll->previous_end_ms > roll->previous_start_ms) {
        s_beat_loop_led[deck].active = false;
        set_deck_loop(deck, roll->previous_start_ms, roll->previous_end_ms);
    } else {
        esp_err_t rc = audio_engine_deck_clear_loop(deck);
        if (rc == ESP_OK) {
            s_beat_loop_led[deck].active = false;
            ESP_LOGI(TAG, "deck %u shifted beat loop released -> clear loop",
                     (unsigned)deck + 1);
            publish_flx4_led_snapshot(false);
        } else {
            ESP_LOGW(TAG, "deck %u shifted beat loop clear failed: %s",
                     (unsigned)deck + 1,
                     esp_err_to_name(rc));
        }
    }

    memset(roll, 0, sizeof(*roll));
}

static void on_loop_control(uint8_t deck, ctrl_deck_control_t control, deck_state_t *state)
{
    if (deck >= DECK_CORE_DECK_COUNT || !state) {
        return;
    }

    deck_loop_shadow_t *shadow = &s_loop_shadow[deck];
    uint32_t position_ms = quantized_deck_position_ms(deck, state);
    bool active = false;
    uint32_t start_ms = 0;
    uint32_t end_ms = 0;

    switch (control) {
    case CTRL_DECK_CTL_LOOP_IN:
        s_beat_loop_led[deck].active = false;
        shadow->pending_in = true;
        shadow->pending_start_ms = position_ms;
        ESP_LOGI(TAG, "deck %u loop in -> %lu ms",
                 (unsigned)deck + 1,
                 (unsigned long)position_ms);
        publish_flx4_led_snapshot(false);
        return;

    case CTRL_DECK_CTL_LOOP_OUT:
        if (shadow->pending_in && position_ms > shadow->pending_start_ms) {
            s_beat_loop_led[deck].active = false;
            set_deck_loop(deck, shadow->pending_start_ms, position_ms);
            shadow->pending_in = false;
        } else {
            ESP_LOGW(TAG, "deck %u loop out ignored: invalid in/out %lu/%lu ms",
                     (unsigned)deck + 1,
                     (unsigned long)shadow->pending_start_ms,
                     (unsigned long)position_ms);
        }
        return;

    case CTRL_DECK_CTL_RELOOP_EXIT:
        if (!read_active_loop(deck, &active, &start_ms, &end_ms)) {
            return;
        }
        if (active) {
            remember_last_loop(deck, start_ms, end_ms);
            esp_err_t rc = audio_engine_deck_clear_loop(deck);
            if (rc == ESP_OK) {
                s_beat_loop_led[deck].active = false;
                ESP_LOGI(TAG, "deck %u loop exit", (unsigned)deck + 1);
                publish_flx4_led_snapshot(false);
            } else {
                ESP_LOGW(TAG, "deck %u loop exit failed: %s",
                         (unsigned)deck + 1,
                         esp_err_to_name(rc));
            }
        } else if (shadow->last_valid) {
            s_beat_loop_led[deck].active = false;
            set_deck_loop(deck, shadow->last_start_ms, shadow->last_end_ms);
        }
        return;

    case CTRL_DECK_CTL_LOOP_HALVE:
    case CTRL_DECK_CTL_LOOP_DOUBLE:
        if (!read_active_loop(deck, &active, &start_ms, &end_ms) || !active || end_ms <= start_ms) {
            return;
        }
        {
            uint32_t duration = end_ms - start_ms;
            uint32_t next_duration = duration;
            if (control == CTRL_DECK_CTL_LOOP_HALVE) {
                if (duration < 2u) {
                    return;
                }
                next_duration = duration / 2u;
            } else {
                if (duration > UINT32_MAX - start_ms || duration > (UINT32_MAX - start_ms) / 2u) {
                    return;
                }
                next_duration = duration * 2u;
            }
            s_beat_loop_led[deck].active = false;
            set_deck_loop(deck, start_ms, start_ms + next_duration);
        }
        return;

    default:
        return;
    }
}

static void on_loop_size_delta(uint8_t deck, int16_t delta, deck_state_t *state)
{
    uint32_t steps = delta < 0 ? (uint32_t)(-(int32_t)delta) : (uint32_t)delta;
    /* A uint32_t millisecond loop reaches its minimum or overflow guard in at
     * most 32 binary steps. Bound a coalesced encoder burst so the deck task
     * cannot spend unbounded time replaying a saturated relative delta. */
    if (steps > 32u) {
        steps = 32u;
    }
    const ctrl_deck_control_t control = delta < 0 ? CTRL_DECK_CTL_LOOP_HALVE
                                                  : CTRL_DECK_CTL_LOOP_DOUBLE;
    for (uint32_t i = 0u; i < steps; ++i) {
        on_loop_control(deck, control, state);
    }
}

static button_id_t button_for_event(const ctrl_event_t *ev)
{
    if (ev && (ev->id == CTRL_ID_LOAD_DECK1 ||
               ev->id == CTRL_ID_LOAD_DECK2 ||
               ev->id == CTRL_ID_SHIFT_LOAD_DECK1 ||
               ev->id == CTRL_ID_SHIFT_LOAD_DECK2)) {
        return BTN_LOAD;
    }
    if (ev && control_link_id_is_deck(ev->id)) {
        switch (control_link_id_control(ev->id)) {
        case CTRL_DECK_CTL_PLAY:
            return BTN_PLAY;
        case CTRL_DECK_CTL_CUE:
            return BTN_CUE;
        default:
            return BTN_COUNT;
        }
    }
    return (button_id_t)(ev ? ev->id : BTN_COUNT);
}

// ─── LED sync ─────────────────────────────────────────────────────────────────

static bool deck_has_loaded_track(uint8_t deck);

/* Every deck LED leaves through here. Beat-Jump pad LEDs are a pure function of
 * the deck's current pad mode, shift state and whether a track is loaded, so the
 * value is recomputed at the point of send rather than trusted from whatever the
 * caller happened to pass — several callers derive it from cached state that can
 * lag a mode change or a track unload.
 *
 * This used to be a #define over control_link_send_led_deck applied to the whole
 * translation unit from a wrapper file, which meant reading any call site here
 * gave the wrong answer about what was actually sent. */
static void deck_send_led(led_id_t led, uint8_t state, uint8_t deck)
{
    if (deck < DECK_CORE_DECK_COUNT) {
        const bool beat_jump_mode = s_decks[deck].pad_mode == CTRL_PAD_MODE_BEAT_JUMP;
        const bool loaded = deck_has_loaded_track(deck);
        if (led >= LED_BEAT_JUMP_PAD_1 && led <= LED_BEAT_JUMP_PAD_8) {
            state = beat_jump_mode && loaded ? 1u : 0u;
        } else if (led >= LED_BEAT_JUMP_SHIFT_HELPER_7 &&
                   led <= LED_BEAT_JUMP_SHIFT_HELPER_8) {
            state = beat_jump_mode && s_deck_shift_held[deck] && loaded ? 1u : 0u;
        }
    }
    control_link_send_led_deck(led, state, deck);
}

static void sync_legacy_compat_leds(uint8_t deck)
{
    if (deck != DECK_CORE_COMPAT_DECK) return;
    deck_state_t *state = &s_decks[deck];
    control_link_send_led(LED_PLAY, state->playing ? 1 : 0);
    control_link_send_led(LED_CUE,  (state->position_ms == state->cue_point_ms) ? 1 : 0);
}

static esp_err_t send_snapshot_led(led_id_t led, uint8_t state, uint8_t deck, void *ctx)
{
    (void)ctx;
    bool performance_pad =
        (led >= LED_BEAT_LOOP_PAD_1 && led <= LED_BEAT_LOOP_PAD_8) ||
        (led >= LED_PAD_FX1_PAD_1 && led <= LED_PAD_FX2_PAD_8) ||
        (led >= LED_HOT_CUE_PAD_1 && led <= LED_HOT_CUE_PAD_8);
    if (s_snapshot_suppress_inactive_pads && performance_pad && state == 0u) {
        return ESP_OK;
    }
    deck_send_led(led, state, deck);
    return ESP_OK;
}

static void send_momentary_led(led_id_t led, uint8_t deck)
{
    deck_send_led(led, 1u, deck);
    deck_send_led(led, 0u, deck);
}

static led_id_t track_load_led_for_deck(uint8_t deck)
{
    return deck == CTRL_DECK_2 ? LED_TRACK_LOAD_DECK2 : LED_TRACK_LOAD_DECK1;
}

static led_id_t beat_jump_pad_led_for_pad(uint8_t pad)
{
    return (led_id_t)(LED_BEAT_JUMP_PAD_1 + pad);
}

static led_id_t beat_jump_shift_helper_led_for_pad(uint8_t pad)
{
    return (led_id_t)(LED_BEAT_JUMP_SHIFT_HELPER_7 + pad);
}

static bool deck_has_loaded_track(uint8_t deck)
{
    audio_engine_deck_status_t status = { 0 };
    return audio_engine_deck_get_status(deck, &status) == ESP_OK && status.loaded;
}

static void publish_track_load_leds(bool force)
{
    for (uint8_t deck = 0; deck < DECK_CORE_DECK_COUNT; deck++) {
        uint8_t value = deck_has_loaded_track(deck) ? 1u : 0u;

        if (force ||
            !s_track_load_led_valid[deck] ||
            s_track_load_led_state[deck] != value) {
            deck_send_led(track_load_led_for_deck(deck), value, deck);
            s_track_load_led_state[deck] = value;
            s_track_load_led_valid[deck] = true;
        }
    }
}

static void publish_beat_jump_pad_leds_for_deck(uint8_t deck, bool force)
{
    if (deck >= DECK_CORE_DECK_COUNT) return;
    deck_state_t state = deck_core_get_deck_state(deck);
    uint8_t value = (state.pad_mode == CTRL_PAD_MODE_BEAT_JUMP &&
                     deck_has_loaded_track(deck)) ? 1u : 0u;
    for (uint8_t pad = 0; pad < 8; pad++) {
        if (force ||
            !s_beat_jump_pad_led_valid[deck][pad] ||
            s_beat_jump_pad_led_state[deck][pad] != value) {
            if (force && value == 0u) {
                s_beat_jump_pad_led_state[deck][pad] = 0u;
                s_beat_jump_pad_led_valid[deck][pad] = true;
                continue;
            }
            deck_send_led(beat_jump_pad_led_for_pad(pad), value, deck);
            s_beat_jump_pad_led_state[deck][pad] = value;
            s_beat_jump_pad_led_valid[deck][pad] = true;
        }
    }
}

static void publish_beat_jump_pad_leds(bool force)
{
    for (uint8_t deck = 0; deck < DECK_CORE_DECK_COUNT; deck++) {
        publish_beat_jump_pad_leds_for_deck(deck, force);
    }
}

static void publish_beat_jump_shift_helper_leds_for_deck(uint8_t deck, bool force)
{
    if (deck >= DECK_CORE_DECK_COUNT) return;
    deck_state_t state = deck_core_get_deck_state(deck);
    uint8_t value = (state.pad_mode == CTRL_PAD_MODE_BEAT_JUMP &&
                     s_deck_shift_held[deck] &&
                     deck_has_loaded_track(deck)) ? 1u : 0u;
    for (uint8_t pad = 0; pad < 2; pad++) {
        if (force ||
            !s_beat_jump_shift_helper_led_valid[deck][pad] ||
            s_beat_jump_shift_helper_led_state[deck][pad] != value) {
            if (force && value == 0u) {
                s_beat_jump_shift_helper_led_state[deck][pad] = 0u;
                s_beat_jump_shift_helper_led_valid[deck][pad] = true;
                continue;
            }
            deck_send_led(beat_jump_shift_helper_led_for_pad(pad), value, deck);
            s_beat_jump_shift_helper_led_state[deck][pad] = value;
            s_beat_jump_shift_helper_led_valid[deck][pad] = true;
        }
    }
}

static void publish_beat_jump_shift_helper_leds(bool force)
{
    for (uint8_t deck = 0; deck < DECK_CORE_DECK_COUNT; deck++) {
        publish_beat_jump_shift_helper_leds_for_deck(deck, force);
    }
}

static void publish_flx4_led_snapshot(bool force)
{
    flx4_led_snapshot_input_t input = { 0 };
    input.smart_cfx = audio_engine_get_smart_cfx_enabled() ? 1u : 0u;
    input.smart_fader = audio_engine_get_smart_fader_enabled() ? 1u : 0u;
    input.beat_fx_on = s_beat_fx.enabled ? 1u : 0u;
    input.master_cue = audio_engine_get_master_cue_enabled() ? 1u : 0u;

    for (uint8_t deck = 0; deck < DECK_CORE_DECK_COUNT; deck++) {
        deck_state_t state = deck_core_get_deck_state(deck);
        input.cue[deck] = state.position_ms == state.cue_point_ms ? 1 : 0;
        input.play[deck] = state.playing ? 1 : 0;
        input.pfl[deck] = audio_engine_get_pfl_enabled(deck) ? 1 : 0;
        input.sync[deck] = state.sync_enabled ? 1 : 0;
        input.pad_mode[deck] = state.pad_mode;
        input.censor_active[deck] = state.censor_active ? 1u : 0u;
        input.loop_in_marker[deck] = s_loop_shadow[deck].pending_in ? 1 : 0;
        input.beat_loop_pad_active[deck] = s_beat_loop_led[deck].active ? 1u : 0u;
        input.beat_loop_active_pad[deck] = s_beat_loop_led[deck].pad;
        input.hot_cue_exists_mask[deck] = hot_cue_exists_mask_for_deck(deck);
        input.pad_fx_active[deck] = s_pad_fx_led[deck].active ? 1u : 0u;
        input.pad_fx_active_mode[deck] = s_pad_fx_led[deck].mode;
        input.pad_fx_active_pad[deck] = s_pad_fx_led[deck].pad;

        bool loop_active = false;
        uint32_t loop_start = 0;
        uint32_t loop_end = 0;
        if (audio_engine_deck_get_loop_state(deck, &loop_active, &loop_start, &loop_end) == ESP_OK) {
            input.loop_active[deck] = loop_active ? 1 : 0;
            input.loop_start_ms[deck] = loop_start;
            input.loop_end_ms[deck] = loop_end;
        }
    }

    /* A reconnect snapshot used to send every inactive pad bank as eight
     * sequential OFF notes, which the FLX4 renders as a visible sweep. The
     * controller starts with inactive pads off; returning success here still
     * primes the publisher's diff cache without putting those bursts on USB. */
    /* State-driven LED values come from actor-owned live data, not from the
     * published seqlock snapshot the UI and web readers consume: this runs
     * inside the deck actor, where s_decks and the audio engine are current and
     * the snapshot may not be yet.
     *
     * Publish the snapshot before handing the input to the publisher, because
     * publish_track_load_leds() and the Beat-Jump cache helpers below run after
     * this returns and must compute from the same values that were actually
     * sent — including across a track unload. */
    for (uint8_t deck = 0; deck < DECK_CORE_DECK_COUNT; ++deck) {
        deck_state_t state = s_decks[deck];
        if (deck_uses_audio_engine(deck)) {
            state.playing = audio_engine_deck_is_playing(deck);
            state.position_ms = audio_engine_deck_position_ms(deck);
        }
        input.cue[deck] = state.position_ms == state.cue_point_ms ? 1u : 0u;
        input.play[deck] = state.playing ? 1u : 0u;
        input.sync[deck] = state.sync_enabled ? 1u : 0u;
        input.pad_mode[deck] = state.pad_mode;
        input.censor_active[deck] = state.censor_active ? 1u : 0u;
        input.loop_in_marker[deck] = s_loop_shadow[deck].pending_in ? 1u : 0u;
    }
    publish_state_snapshot();

    s_snapshot_suppress_inactive_pads = force;
    esp_err_t rc = flx4_led_publisher_publish(&s_flx4_led_publisher,
                                              &input,
                                              force,
                                              send_snapshot_led,
                                              NULL);
    s_snapshot_suppress_inactive_pads = false;
    if (rc != ESP_OK) {
        ESP_LOGW(TAG, "FLX4 LED snapshot publish failed: %s", esp_err_to_name(rc));
    } else {
        ESP_LOGD(TAG, "%s FLX4 LED snapshot published", force ? "forced" : "diff");
    }
    publish_track_load_leds(force);
    publish_beat_jump_pad_leds(force);
    publish_beat_jump_shift_helper_leds(force);
}

static void execute_ui_command(const deck_ui_command_t *cmd)
{
    if (!cmd) return;
    switch (cmd->kind) {
    case DECK_UI_CMD_LOAD_SELECTED:
    {
        bool loaded = false;
        if (ui_library_load_selected_for_deck) {
            esp_err_t rc = ui_library_load_selected_for_deck(cmd->deck);
            ESP_LOGI(TAG, "deck %u load selected -> %s", (unsigned)cmd->deck + 1,
                     esp_err_to_name(rc));
            loaded = rc == ESP_OK;
        } else if (cmd->deck == DECK_CORE_COMPAT_DECK && ui_library_load_selected) {
            esp_err_t rc = ui_library_load_selected();
            ESP_LOGI(TAG, "load selected -> %s", esp_err_to_name(rc));
            loaded = rc == ESP_OK;
        } else {
            ESP_LOGW(TAG, "load selected unsupported: UI API unavailable");
        }
        if (loaded) {
            /* Refresh only state owned by the newly loaded track. A full
             * dual-deck snapshot here can emit unrelated D1 pad state during
             * the first D2 load after boot. */
            publish_loaded_track_hot_cue_leds(cmd->deck);
            publish_track_load_leds(false);
            publish_beat_jump_pad_leds_for_deck(cmd->deck, false);
            publish_beat_jump_shift_helper_leds_for_deck(cmd->deck, false);
        }
        break;
    }

    case DECK_UI_CMD_LIBRARY_SELECT_DELTA_IF_ACTIVE:
        if (ui_is_library_active && ui_library_select_delta && ui_is_library_active()) {
            esp_err_t rc = ui_library_select_delta(cmd->value);
            ESP_LOGD(TAG, "track select %+d -> %s", (int)cmd->value, esp_err_to_name(rc));
        } else {
            ESP_LOGD(TAG, "track select %+d pressed outside library tab", (int)cmd->value);
        }
        break;

    case DECK_UI_CMD_BROWSE_DELTA: {
        bool shifted = cmd->id == CTRL_ID_BROWSE_SHIFT_DELTA;
        bool library_active = !ui_is_library_active || ui_is_library_active();
        bool overview_active = ui_is_overview_active && ui_is_overview_active();
        if (library_active && ui_library_select_delta) {
            int scaled = shifted ? cmd->value * BROWSE_SHIFT_LIBRARY_MULTIPLIER : cmd->value;
            esp_err_t rc = ui_library_select_delta(scaled);
            ESP_LOGD(TAG, "browse %+d -> %s", scaled, esp_err_to_name(rc));
        } else if (!library_active && overview_active && ui_overview_zoom_delta) {
            int scaled = shifted ? cmd->value * BROWSE_SHIFT_OVERVIEW_MULTIPLIER : cmd->value;
            esp_err_t rc = ui_overview_zoom_delta(scaled);
            ESP_LOGD(TAG, "overview zoom %+d -> %s", scaled, esp_err_to_name(rc));
        } else {
            ESP_LOGW(TAG, "browse unsupported: UI API unavailable");
        }
        break;
    }

    case DECK_UI_CMD_TOGGLE_LIBRARY_VIEW:
        if (ui_toggle_library_view) {
            esp_err_t rc = ui_toggle_library_view();
            ESP_LOGD(TAG, "browse press -> library/overview toggle: %s", esp_err_to_name(rc));
        } else if (ui_show_library) {
            esp_err_t rc = ui_show_library();
            ESP_LOGD(TAG, "browse press -> library fallback: %s", esp_err_to_name(rc));
        } else {
            ESP_LOGW(TAG, "browse press unsupported: UI API unavailable");
        }
        break;

    case DECK_UI_CMD_SHOW_LIBRARY:
        if (ui_show_library) {
            esp_err_t rc = ui_show_library();
            ESP_LOGD(TAG, "browse shift press -> library: %s", esp_err_to_name(rc));
        } else if (ui_toggle_library_view) {
            esp_err_t rc = ui_toggle_library_view();
            ESP_LOGD(TAG, "browse shift press fallback -> toggle: %s", esp_err_to_name(rc));
        } else {
            ESP_LOGW(TAG, "browse shift press unsupported: UI API unavailable");
        }
        break;
    }
}

static bool enqueue_ui_command(const deck_ui_command_t *cmd)
{
    if (!cmd) return false;
#if defined(DECK_CORE_PC_TEST)
    if (s_test_ui_command_count >= DECK_CORE_TEST_UI_COMMAND_QUEUE_LEN) {
        ESP_LOGW(TAG, "test UI command queue full");
        return false;
    }
    s_test_ui_commands[s_test_ui_command_count++] = *cmd;
    return true;
#else
    if (!s_ui_command_queue) {
        ESP_LOGW(TAG, "UI command queue unavailable");
        return false;
    }
    if (xQueueSend(s_ui_command_queue, cmd, 0) != pdTRUE) {
        ESP_LOGW(TAG, "UI command queue full");
        return false;
    }
    return true;
#endif
}

static void handle_jog_touch(uint8_t deck, bool pressed, deck_state_t *state);

static void on_state_event(const ctrl_event_t *ev)
{
    if (!ev) {
        return;
    }
    if (ev->id == CTRL_ID_S3_DEBUG_AP) {
        if (ev->value == CTRL_S3_DEBUG_AP_OFF ||
            ev->value == CTRL_S3_DEBUG_AP_STARTING ||
            ev->value == CTRL_S3_DEBUG_AP_ERROR) {
            s_s3_debug_token_hi = 0u;
            s_s3_debug_token_hi_valid = false;
            if (s_s3_debug_ap_token_cb) s_s3_debug_ap_token_cb(0u);
        }
        if (s_s3_debug_ap_status_cb) {
            s_s3_debug_ap_status_cb((uint8_t)ev->value);
        }
        ESP_LOGI(TAG, "S3 Debug AP status=%d", ev->value);
        return;
    }
    if (ev->id == CTRL_ID_S3_DEBUG_TOKEN_HI) {
        if (ev->value >= 0 && ev->value <= 999) {
            s_s3_debug_token_hi = (uint16_t)ev->value;
            s_s3_debug_token_hi_valid = true;
        }
        return;
    }
    if (ev->id == CTRL_ID_S3_DEBUG_TOKEN_LO) {
        if (s_s3_debug_token_hi_valid && ev->value >= 0 && ev->value <= 999) {
            uint32_t token = (uint32_t)s_s3_debug_token_hi * 1000u +
                             (uint16_t)ev->value;
            s_s3_debug_token_hi_valid = false;
            if (s_s3_debug_ap_token_cb &&
                (token == 0u || (token >= 100000u && token <= 999999u))) {
                s_s3_debug_ap_token_cb(token);
            }
        }
        return;
    }
    if (ev->id != CTRL_ID_FLX4_CONNECTION) {
        ESP_LOGW(TAG, "unknown state id %u", (unsigned)ev->id);
        return;
    }
    if (ev->value == CTRL_FLX4_CONNECTED) {
        if (!s_flx4_connection_state_valid || !s_flx4_connected) {
            ESP_LOGI(TAG, "FLX4 connected; forcing LED snapshot");
            publish_flx4_led_snapshot(true);
        }
        s_flx4_connection_state_valid = true;
        s_flx4_connected = true;
    } else if (ev->value == CTRL_FLX4_DISCONNECTED) {
        if (!s_flx4_connection_state_valid || s_flx4_connected) {
            ESP_LOGI(TAG, "FLX4 disconnected");
        }
        s_flx4_connection_state_valid = true;
        s_flx4_connected = false;
        /* A physical disconnect cannot deliver the final Note-On(value=0)
         * touch edge. Force both platters released so scratch/capture never
         * remain latched and silent until the next track load. */
        for (uint8_t deck = 0; deck < DECK_CORE_DECK_COUNT; deck++) {
            handle_jog_touch(deck, false, &s_decks[deck]);
        }
    } else {
        ESP_LOGW(TAG, "unknown FLX4 connection state %d", ev->value);
    }
}

static bool on_system_button(const ctrl_event_t *ev)
{
    if (!ev || ev->type != CTRL_EV_BUTTON) {
        return false;
    }

    switch (ev->id) {
    case CTRL_ID_SMART_CFX:
        if (ev->value == 0) {
            return true;
        }
        audio_engine_toggle_smart_cfx();
        deck_send_led(LED_SMART_CFX,
                                   audio_engine_get_smart_cfx_enabled() ? 1u : 0u,
                                   CTRL_DECK_1);
        return true;
    case CTRL_ID_SMART_FADER:
        if (ev->value == 0) {
            return true;
        }
        audio_engine_toggle_smart_fader();
        deck_send_led(LED_SMART_FADER,
                                   audio_engine_get_smart_fader_enabled() ? 1u : 0u,
                                   CTRL_DECK_1);
        return true;
    case CTRL_ID_SMART_CFX_SHIFT:
    case CTRL_ID_SMART_FADER_SHIFT:
        return true;
    case CTRL_ID_MASTER_CUE:
        if (ev->value == 0) {
            return true;
        }
        audio_engine_toggle_master_cue();
        deck_send_led(LED_MASTER_CUE,
                                   audio_engine_get_master_cue_enabled() ? 1u : 0u,
                                   CTRL_DECK_1);
        return true;
    case CTRL_ID_BEAT_FX_SELECT_NEXT:
        if (ev->value != 0) {
            s_beat_fx.effect = beat_fx_next_effect(s_beat_fx.effect);
            sync_beat_fx_audio_state();
            ESP_LOGI(TAG, "beat fx effect -> %d", (int)s_beat_fx.effect);
        }
        return true;
    case CTRL_ID_BEAT_FX_SELECT_PREV:
        if (ev->value != 0) {
            s_beat_fx.effect = beat_fx_previous_effect(s_beat_fx.effect);
            sync_beat_fx_audio_state();
            ESP_LOGI(TAG, "beat fx effect -> %d", (int)s_beat_fx.effect);
        }
        return true;
    case CTRL_ID_BEAT_FX_BEAT_DEC:
        if (ev->value != 0 && s_beat_fx.beat > DECK_CORE_BEAT_FX_BEAT_1_4) {
            s_beat_fx.beat = (deck_core_beat_fx_beat_t)(s_beat_fx.beat - 1);
            sync_beat_fx_audio_state();
            ESP_LOGI(TAG, "beat fx beat -> %d", (int)s_beat_fx.beat);
        }
        return true;
    case CTRL_ID_BEAT_FX_BEAT_INC:
        if (ev->value != 0 && s_beat_fx.beat < DECK_CORE_BEAT_FX_BEAT_4) {
            s_beat_fx.beat = (deck_core_beat_fx_beat_t)(s_beat_fx.beat + 1);
            sync_beat_fx_audio_state();
            ESP_LOGI(TAG, "beat fx beat -> %d", (int)s_beat_fx.beat);
        }
        return true;
    case CTRL_ID_BEAT_FX_BEAT_DEC_SHIFT:
        if (ev->value != 0) {
            deck_core_beat_fx_beat_t next = s_beat_fx.beat;
            if (next > DECK_CORE_BEAT_FX_BEAT_1_2) {
                next = (deck_core_beat_fx_beat_t)(next - 2);
            } else {
                next = DECK_CORE_BEAT_FX_BEAT_1_4;
            }
            if (next != s_beat_fx.beat) {
                s_beat_fx.beat = next;
                sync_beat_fx_audio_state();
                ESP_LOGI(TAG, "beat fx beat -> %d", (int)s_beat_fx.beat);
            }
        }
        return true;
    case CTRL_ID_BEAT_FX_BEAT_INC_SHIFT:
        if (ev->value != 0) {
            deck_core_beat_fx_beat_t next = s_beat_fx.beat;
            if (next < DECK_CORE_BEAT_FX_BEAT_2) {
                next = (deck_core_beat_fx_beat_t)(next + 2);
            } else {
                next = DECK_CORE_BEAT_FX_BEAT_4;
            }
            if (next != s_beat_fx.beat) {
                s_beat_fx.beat = next;
                sync_beat_fx_audio_state();
                ESP_LOGI(TAG, "beat fx beat -> %d", (int)s_beat_fx.beat);
            }
        }
        return true;
    case CTRL_ID_BEAT_FX_TARGET:
        if (ev->value >= CTRL_BEAT_FX_TARGET_CH1 && ev->value <= CTRL_BEAT_FX_TARGET_BOTH) {
            s_beat_fx.target = (ctrl_beat_fx_target_t)ev->value;
            sync_beat_fx_audio_state();
            ESP_LOGI(TAG, "beat fx target -> %d", (int)s_beat_fx.target);
        }
        return true;
    case CTRL_ID_BEAT_FX_ON:
        if (ev->value != 0) {
            s_beat_fx.enabled = !s_beat_fx.enabled;
            sync_beat_fx_audio_state();
            deck_send_led(LED_BEAT_FX_ON,
                                       s_beat_fx.enabled ? 1u : 0u,
                                       CTRL_DECK_1);
            ESP_LOGI(TAG, "beat fx -> %s", s_beat_fx.enabled ? "ON" : "OFF");
        }
        return true;
    case CTRL_ID_BEAT_FX_CLEAR:
        if (ev->value != 0) {
            init_beat_fx_state();
            sync_beat_fx_audio_state();
            deck_send_led(LED_BEAT_FX_ON, 0u, CTRL_DECK_1);
            ESP_LOGI(TAG, "beat fx reset");
        }
        return true;
    default:
        return false;
    }
}

static bool on_system_value(const ctrl_event_t *ev)
{
    if (!ev || ev->type != CTRL_EV_PITCH) {
        return false;
    }

    if (ev->id == CTRL_ID_HEADPHONE_LEVEL) {
        audio_engine_set_headphone_level(ev->value < 0 ? 0u : (uint16_t)ev->value);
        return true;
    }

    if (ev->id != CTRL_ID_BEAT_FX_DEPTH) {
        return false;
    }

    int16_t depth = ev->value;
    if (depth < 0) {
        depth = 0;
    } else if (depth > 127) {
        depth = 127;
    }
    s_beat_fx.depth = (uint8_t)depth;
    sync_beat_fx_audio_state();
    return true;
}

// ─── Event handlers ───────────────────────────────────────────────────────────

static void on_button(uint8_t deck, button_id_t btn, bool pressed)
{
    if (!pressed) return;

    deck_state_t *state = &s_decks[normalize_deck(deck)];
    bool uses_audio = deck_uses_audio_engine(deck);

    switch (btn) {
    case BTN_PLAY:
        if (uses_audio && audio_engine_deck_is_playing(deck)) {
            esp_err_t rc = audio_engine_deck_pause(deck);
            if (rc == ESP_OK) {
                state->playing = false;
            } else {
                ESP_LOGW(TAG, "deck %u pause failed: %s", (unsigned)deck + 1,
                         esp_err_to_name(rc));
            }
        } else {
            if (uses_audio) {
                esp_err_t rc = audio_engine_deck_play(deck);
                if (rc == ESP_OK) {
                    state->playing = true;
                } else {
                    ESP_LOGW(TAG, "deck %u play failed: %s", (unsigned)deck + 1,
                             esp_err_to_name(rc));
                }
            } else {
                state->playing = !state->playing;
            }
        }
        ESP_LOGI(TAG, "deck %u play -> %s", (unsigned)deck + 1,
                 state->playing ? "PLAYING" : "PAUSED");
        sync_legacy_compat_leds(deck);
        break;

    case BTN_CUE:
        // Return to the cue point (track start by default) and pause — works
        // whether the deck is playing or already paused. Custom cue points are
        // handled by the hot-cue pads, so CUE here is a reliable "back to cue".
        if (uses_audio) {
            audio_engine_deck_pause(deck);
            audio_engine_deck_seek(deck, state->cue_point_ms);
        }
        state->playing     = false;
        state->position_ms = state->cue_point_ms;
        ESP_LOGI(TAG, "deck %u cue -> %lu ms (paused)", (unsigned)deck + 1,
                 (unsigned long)state->cue_point_ms);
        sync_legacy_compat_leds(deck);
        break;

    case BTN_MODE:
        state->perf_mode = (perf_mode_t)((state->perf_mode + 1) % PERF_MODE_COUNT);
        ESP_LOGI(TAG, "deck %u perf mode -> %d", (unsigned)deck + 1, state->perf_mode);
        break;

    case BTN_MASTER_TEMPO:
        state->master_tempo = !state->master_tempo;
        if (uses_audio) {
            audio_engine_deck_set_master_tempo(deck, state->master_tempo);
        }
        ESP_LOGI(TAG, "deck %u master tempo -> %s", (unsigned)deck + 1,
                 state->master_tempo ? "ON" : "OFF");
        break;

    case BTN_EJECT:
        if (uses_audio) {
            audio_engine_deck_stop(deck);
        }
        state->playing      = false;
        state->position_ms  = 0;
        state->cue_point_ms = 0;
        ESP_LOGI(TAG, "deck %u eject", (unsigned)deck + 1);
        sync_legacy_compat_leds(deck);
        break;

    case BTN_LOAD:
        (void)enqueue_ui_command(&(deck_ui_command_t) {
            .kind = DECK_UI_CMD_LOAD_SELECTED,
            .deck = deck,
        });
        break;

    case BTN_TRACK_PREV:
    case BTN_TRACK_NEXT:
        (void)enqueue_ui_command(&(deck_ui_command_t) {
            .kind = DECK_UI_CMD_LIBRARY_SELECT_DELTA_IF_ACTIVE,
            .value = (btn == BTN_TRACK_NEXT) ? 1 : -1,
        });
        break;

    case BTN_SEARCH_BACK:
    case BTN_SEARCH_FWD: {
        uint32_t current = uses_audio ? audio_engine_deck_position_ms(deck) : state->position_ms;
        int32_t target = (int32_t)current + (btn == BTN_SEARCH_FWD ? SEARCH_STEP_MS : -SEARCH_STEP_MS);
        if (target < 0) target = 0;
        esp_err_t rc = uses_audio ? audio_engine_deck_seek(deck, (uint32_t)target) : ESP_OK;
        if (rc == ESP_OK) {
            state->position_ms = (uint32_t)target;
            ESP_LOGI(TAG, "deck %u search %s -> %lu ms", (unsigned)deck + 1,
                     btn == BTN_SEARCH_FWD ? "fwd" : "back",
                     (unsigned long)state->position_ms);
        } else {
            ESP_LOGW(TAG, "search %s failed: %s",
                     btn == BTN_SEARCH_FWD ? "fwd" : "back",
                     esp_err_to_name(rc));
        }
        break;
    }

    case BTN_PERF1:
    case BTN_PERF2:
    case BTN_PERF3:
    case BTN_HOLD:
        ESP_LOGD(TAG, "perf btn %d pressed in mode %d (UI action unsupported)",
                 btn, state->perf_mode);
        break;

    default:
        ESP_LOGD(TAG, "btn %d pressed (unhandled in MVP)", btn);
        break;
    }
}

#if !defined(DECK_CORE_PC_TEST)
static uint8_t peak_to_midi_level(uint16_t peak)
{
    uint32_t level = ((uint32_t)peak * 127u) / 32768u;
    return (uint8_t)(level > 127u ? 127u : level);
}

/* VU meter ballistics: the raw per-30 ms-window peak jumps around wildly on
 * music, so the LED VU flickered. Rise instantly to a new peak, then fall back
 * by a fixed step each tick (~330 ms full release) for a natural meter. */
#define VU_DECAY_STEP_PER_TICK 12u
static uint8_t s_vu_display_level[DECK_CORE_DECK_COUNT];

static uint8_t vu_ballistic_level(uint8_t deck, uint8_t raw)
{
    uint8_t prev = (deck < DECK_CORE_DECK_COUNT) ? s_vu_display_level[deck] : 0u;
    uint8_t disp;
    if (raw >= prev) {
        disp = raw;                          /* instant attack */
    } else if (prev > VU_DECAY_STEP_PER_TICK) {
        disp = (uint8_t)(prev - VU_DECAY_STEP_PER_TICK);
        if (disp < raw) {
            disp = raw;
        }
    } else {
        disp = raw;
    }
    if (deck < DECK_CORE_DECK_COUNT) {
        s_vu_display_level[deck] = disp;
    }
    return disp;
}

/* VU meters run in their own task, not an esp_timer callback: the send blocks
 * on uart_write_bytes when the TX ring is full (e.g. during a bulk profile
 * transfer), and blocking inside an esp_timer callback stalls every other
 * timer in the system. The task also gates on FLX4-connected + at-least-one-
 * deck-playing so it never floods the link while idle. */
static void vu_task(void *arg)
{
    (void)arg;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(30));

        if (!s_flx4_connected) {
            s_vu_meters_active = false;
            continue;
        }

        bool any_playing = false;
        for (uint8_t deck = 0; deck < DECK_CORE_DECK_COUNT; deck++) {
            if (audio_engine_deck_is_playing(deck)) {
                any_playing = true;
                break;
            }
        }

        if (!any_playing) {
            if (!s_vu_meters_active) {
                continue;   /* already idle; nothing to send */
            }
            s_vu_meters_active = false;   /* emit one final zeroed frame set */
        } else {
            s_vu_meters_active = true;
        }

        for (uint8_t deck = 0; deck < DECK_CORE_DECK_COUNT; deck++) {
            uint8_t level;
            if (any_playing) {
                uint8_t raw = peak_to_midi_level(audio_engine_get_deck_peak(deck));
                level = vu_ballistic_level(deck, raw);
            } else {
                level = 0u;                       /* snap to 0 when stopped */
                s_vu_display_level[deck] = 0u;
            }
            deck_send_led(LED_VU_METER, level, deck);
        }
    }
}
#endif

/*
 * Jog platter touch (vinyl mode Phase 1). Touching the platter top while the
 * deck plays enters "platter-hold": the audio is silenced and the position
 * frozen (audio_engine hold), so jogs scrub the position like a held record
 * (see on_jog). Releasing resumes forward playback from wherever it was
 * scrubbed to. Touching while paused just records the touch state (paused jog
 * already scrubs).
 *
 * With CONFIG_AUDIO_SCRATCH_ENABLED (Phase 4) touching during playback instead
 * enters audible scratch: the deck's audio comes from the jog-driven read head
 * over the capture buffer, and release seeks normal playback to the head. The
 * s_jog_hold_active[] flag marks "engaged while playing" in both builds so the
 * matching release action fires.
 */
static void handle_jog_touch(uint8_t deck, bool pressed, deck_state_t *state)
{
    if (deck >= DECK_CORE_DECK_COUNT || !state) {
        return;
    }

    if (pressed) {
        /* Touch is a level on the wire, but scratch begin/end are edge-driven.
         * Ignore a repeated press while already held; otherwise the engine
         * correctly reports "already active" and this layer used to mistake
         * that for begin failure, muting a live scratch via platter-hold. */
        if (s_jog_touched[deck]) {
            return;
        }
        s_jog_touched[deck] = true;
#if CONFIG_AUDIO_SCRATCH_ENABLED
        if (deck_uses_audio_engine(deck)) {
            s_jog_hold_active[deck] = true;
            s_jog_scratch_active[deck] = audio_engine_deck_scratch_begin(deck);
            if (s_jog_scratch_active[deck]) {
                ESP_LOGI(TAG, "deck %u %s scratch begin", (unsigned)deck + 1,
                         state->playing ? "play" : "cue");
            } else if (state->playing) {
                state->position_ms = current_deck_position_ms(deck, state);
                audio_engine_deck_set_hold(deck, true);
                ESP_LOGW(TAG, "deck %u scratch unavailable -> platter hold",
                         (unsigned)deck + 1);
            } else {
                s_jog_hold_active[deck] = false;
            }
        }
#else
        if (state->playing) {
            /* Seed the position from the live playhead so the first scrub delta
             * is relative to where the audio actually is, then freeze it. */
            state->position_ms = current_deck_position_ms(deck, state);
            s_jog_hold_active[deck] = true;
            audio_engine_deck_set_hold(deck, true);
            ESP_LOGI(TAG, "deck %u platter hold -> %lu ms (audio muted)",
                     (unsigned)deck + 1,
                     (unsigned long)state->position_ms);
        }
#endif
        return;
    }

    if (!s_jog_touched[deck]) {
        return; /* duplicate release: no second handoff/hold transition */
    }
    s_jog_touched[deck] = false;
    if (s_jog_hold_active[deck]) {
        s_jog_hold_active[deck] = false;
#if CONFIG_AUDIO_SCRATCH_ENABLED
        if (s_jog_scratch_active[deck]) {
            audio_engine_deck_scratch_end(deck);
            ESP_LOGI(TAG, "deck %u scratch end", (unsigned)deck + 1);
        } else {
            audio_engine_deck_set_hold(deck, false);
        }
        s_jog_scratch_active[deck] = false;
#else
        audio_engine_deck_set_hold(deck, false);
        ESP_LOGI(TAG, "deck %u platter release -> resume %lu ms",
                 (unsigned)deck + 1,
                 (unsigned long)state->position_ms);
#endif
    }
}

static bool on_deck_extension_button(const ctrl_event_t *ev)
{
    if (!ev || ev->type != CTRL_EV_BUTTON || !control_link_id_is_deck(ev->id)) {
        return false;
    }

    uint8_t deck = deck_index_for_event(ev);
    bool pressed = ev->value != 0;
    deck_state_t *state = &s_decks[normalize_deck(deck)];
    bool uses_audio = deck_uses_audio_engine(deck);

    switch (control_link_id_control(ev->id)) {
    case CTRL_DECK_CTL_JOG_TOUCH:
        handle_jog_touch(deck, pressed, state);
        return true;

    case CTRL_DECK_CTL_SHIFT:
        s_deck_shift_held[deck] = pressed;
        ESP_LOGD(TAG, "deck %u shift -> %s", (unsigned)deck + 1,
                 pressed ? "pressed" : "released");
        publish_beat_jump_shift_helper_leds(false);
        return true;

    case CTRL_DECK_CTL_TO_START:
        if (!pressed) {
            return true;
        }
        if (uses_audio) {
            audio_engine_deck_pause(deck);
            audio_engine_deck_seek(deck, 0);
        }
        state->playing = false;
        state->position_ms = 0;
        state->cue_point_ms = 0;
        ESP_LOGI(TAG, "deck %u cue+shift -> track start", (unsigned)deck + 1);
        sync_legacy_compat_leds(deck);
        send_momentary_led(LED_CUE_SHIFT, deck);
        return true;

    case CTRL_DECK_CTL_SYNC:
        if (pressed) {
            bool applied = false;
            if (state->sync_enabled) {
                state->sync_enabled = false;
            } else {
                applied = apply_beat_sync(deck, state);
            }
            (void)applied;
            ESP_LOGI(TAG, "deck %u sync -> %s%s",
                     (unsigned)deck + 1,
                     state->sync_enabled ? "ON" : "OFF",
                     applied ? "" : " (tempo unchanged)");
            publish_flx4_led_snapshot(false);
        }
        return true;

    case CTRL_DECK_CTL_TEMPO_RANGE:
        if (pressed) {
            bool sync_was_enabled = state->sync_enabled;
            state->sync_enabled = false;
            state->tempo_range_percent = next_tempo_range_percent(state->tempo_range_percent);
            apply_deck_pitch(deck, state);
            ESP_LOGI(TAG, "deck %u tempo range -> ±%u%%",
                     (unsigned)deck + 1, (unsigned)state->tempo_range_percent);
            if (sync_was_enabled) {
                publish_flx4_led_snapshot(false);
            }
        }
        return true;

    case CTRL_DECK_CTL_LOOP_IN:
    case CTRL_DECK_CTL_LOOP_OUT:
    case CTRL_DECK_CTL_RELOOP_EXIT:
    case CTRL_DECK_CTL_LOOP_HALVE:
    case CTRL_DECK_CTL_LOOP_DOUBLE:
        if (pressed) {
            on_loop_control(deck, control_link_id_control(ev->id), state);
        }
        return true;

    case CTRL_DECK_CTL_EXT_ACTION:
    {
        uint8_t action = CTRL_DECK_EXT_ACTION(ev->value);
        bool ext_pressed = CTRL_DECK_EXT_PRESSED(ev->value);
        if (action == CTRL_DECK_EXT_ACTION_CENSOR) {
            handle_censor(deck, ext_pressed, state);
            return true;
        }
        if (!ext_pressed) {
            return true;
        }
        switch (action) {
        case CTRL_DECK_EXT_ACTION_SYNC_MASTER:
            set_sync_master(deck, state);
            return true;
        case CTRL_DECK_EXT_ACTION_RELOOP_STOP:
            stop_and_forget_loop(deck);
            return true;
        case CTRL_DECK_EXT_ACTION_LOOP_ADJUST_IN:
            adjust_loop_boundary(deck, true, state);
            send_momentary_led(LED_LOOP_ADJUST_IN, deck);
            return true;
        case CTRL_DECK_EXT_ACTION_LOOP_ADJUST_OUT:
            adjust_loop_boundary(deck, false, state);
            send_momentary_led(LED_LOOP_ADJUST_OUT, deck);
            return true;
        case CTRL_DECK_EXT_ACTION_QUANTIZE:
            state->quantize_enabled = !state->quantize_enabled;
            ESP_LOGI(TAG, "deck %u quantize -> %s",
                     (unsigned)deck + 1,
                     state->quantize_enabled ? "ON" : "OFF");
            return true;
        case CTRL_DECK_EXT_ACTION_SYNC_OFF:
            if (state->sync_enabled) {
                state->sync_enabled = false;
                publish_flx4_led_snapshot(false);
            }
            ESP_LOGI(TAG, "deck %u sync -> OFF", (unsigned)deck + 1);
            return true;
        default:
            return true;
        }
    }

    case CTRL_DECK_CTL_BEAT_JUMP_BACK:
    case CTRL_DECK_CTL_BEAT_JUMP_FORWARD:
        if (pressed) {
            handle_beat_jump(deck,
                             control_link_id_control(ev->id) == CTRL_DECK_CTL_BEAT_JUMP_BACK ? -1 : 1,
                             state);
        }
        return true;

    case CTRL_DECK_CTL_PAD_MODE_HOT_CUE:
        if (pressed) {
            state->perf_mode = PERF_MODE_HOT_CUE;
            state->pad_mode = CTRL_PAD_MODE_HOT_CUE;
            ESP_LOGI(TAG, "deck %u pad mode -> HOT_CUE", (unsigned)deck + 1);
            publish_flx4_led_snapshot(false);
        }
        return true;

    case CTRL_DECK_CTL_PAD_MODE_KEYBOARD:
        if (pressed) {
            ESP_LOGI(TAG, "deck %u KEYBOARD/STEMS pad mode ignored (out of scope)",
                     (unsigned)deck + 1);
        }
        return true;

    case CTRL_DECK_CTL_PAD_MODE_PAD_FX1:
        if (pressed) {
            state->pad_mode = CTRL_PAD_MODE_PAD_FX1;
            ESP_LOGI(TAG, "deck %u pad mode -> PAD_FX1 (behavior deferred)",
                     (unsigned)deck + 1);
            publish_flx4_led_snapshot(false);
        }
        return true;

    case CTRL_DECK_CTL_PAD_MODE_PAD_FX2:
        if (pressed) {
            state->pad_mode = CTRL_PAD_MODE_PAD_FX2;
            ESP_LOGI(TAG, "deck %u pad mode -> PAD_FX2 (behavior deferred)",
                     (unsigned)deck + 1);
            publish_flx4_led_snapshot(false);
        }
        return true;

    case CTRL_DECK_CTL_PAD_MODE_BEAT_LOOP:
        if (pressed) {
            state->perf_mode = PERF_MODE_LOOP_ROLL;
            state->pad_mode = CTRL_PAD_MODE_BEAT_LOOP;
            ESP_LOGI(TAG, "deck %u pad mode -> BEAT_LOOP", (unsigned)deck + 1);
            publish_flx4_led_snapshot(false);
        }
        return true;

    case CTRL_DECK_CTL_PAD_MODE_BEAT_JUMP:
        if (pressed) {
            state->perf_mode = PERF_MODE_BEAT_JUMP;
            state->pad_mode = CTRL_PAD_MODE_BEAT_JUMP;
            ESP_LOGI(TAG, "deck %u pad mode -> BEAT_JUMP", (unsigned)deck + 1);
            publish_flx4_led_snapshot(false);
        }
        return true;

    case CTRL_DECK_CTL_PAD_MODE_KEY_SHIFT:
        if (pressed) {
            ESP_LOGI(TAG, "deck %u KEY_SHIFT pad mode ignored (out of scope)",
                     (unsigned)deck + 1);
        }
        return true;

    case CTRL_DECK_CTL_PAD_MODE_SAMPLER:
        if (pressed) {
            ESP_LOGI(TAG, "deck %u SAMPLER pad mode ignored (out of scope)",
                     (unsigned)deck + 1);
        }
        return true;

    case CTRL_DECK_CTL_PAD_ACTION:
        if (CTRL_PAD_ACTION_PRESSED(ev->value) &&
            CTRL_PAD_ACTION_MODE(ev->value) == CTRL_PAD_MODE_HOT_CUE) {
            handle_hot_cue_pad_action(deck,
                                      CTRL_PAD_ACTION_PAD(ev->value),
                                      CTRL_PAD_ACTION_SHIFTED(ev->value),
                                      state);
        } else if (CTRL_PAD_ACTION_PRESSED(ev->value) &&
                   CTRL_PAD_ACTION_MODE(ev->value) == CTRL_PAD_MODE_BEAT_JUMP &&
                   !CTRL_PAD_ACTION_SHIFTED(ev->value)) {
            int beat_shift = 0;
            if (beat_jump_shift_for_pad(CTRL_PAD_ACTION_PAD(ev->value), &beat_shift)) {
                handle_beat_jump(deck, beat_shift, state);
            }
        } else if (CTRL_PAD_ACTION_PRESSED(ev->value) &&
                   CTRL_PAD_ACTION_MODE(ev->value) == CTRL_PAD_MODE_BEAT_LOOP &&
                   !CTRL_PAD_ACTION_SHIFTED(ev->value)) {
            handle_beat_loop_pad_action(deck, CTRL_PAD_ACTION_PAD(ev->value), state);
        } else if (CTRL_PAD_ACTION_MODE(ev->value) == CTRL_PAD_MODE_BEAT_LOOP &&
                   CTRL_PAD_ACTION_SHIFTED(ev->value)) {
            if (CTRL_PAD_ACTION_PRESSED(ev->value)) {
                handle_shifted_beat_loop_press(deck, CTRL_PAD_ACTION_PAD(ev->value), state);
            } else {
                handle_shifted_beat_loop_release(deck);
            }
        } else if (CTRL_PAD_ACTION_MODE(ev->value) == CTRL_PAD_MODE_PAD_FX1 ||
                   CTRL_PAD_ACTION_MODE(ev->value) == CTRL_PAD_MODE_PAD_FX2) {
            uint8_t pad_fx_mode = CTRL_PAD_ACTION_MODE(ev->value);
            uint8_t pad_fx_pad = CTRL_PAD_ACTION_PAD(ev->value);
            bool pad_fx_pressed = CTRL_PAD_ACTION_PRESSED(ev->value);
            audio_pad_fx_mode_t mode =
                pad_fx_mode == CTRL_PAD_MODE_PAD_FX2
                    ? AUDIO_PAD_FX_MODE_PAD_FX2
                    : AUDIO_PAD_FX_MODE_PAD_FX1;
            esp_err_t rc = audio_engine_set_pad_fx(deck,
                                                   mode,
                                                   pad_fx_pad,
                                                   pad_fx_pressed);
            if (rc != ESP_OK) {
                ESP_LOGW(TAG, "deck %u pad fx route failed: %d",
                         (unsigned)deck + 1, (int)rc);
            } else {
                if (pad_fx_pressed) {
                    s_pad_fx_led[deck].active = true;
                    s_pad_fx_led[deck].mode = pad_fx_mode;
                    s_pad_fx_led[deck].pad = pad_fx_pad;
                } else if (s_pad_fx_led[deck].active &&
                           s_pad_fx_led[deck].mode == pad_fx_mode &&
                           s_pad_fx_led[deck].pad == pad_fx_pad) {
                    s_pad_fx_led[deck].active = false;
                }
                publish_flx4_led_snapshot(false);
            }
        } else if (should_log_deferred_button(ev->id, ev->value)) {
            ESP_LOGI(TAG, "deck %u pad action mode=%u pad=%u shifted=%u (behavior deferred)",
                     (unsigned)deck + 1,
                     (unsigned)CTRL_PAD_ACTION_MODE(ev->value),
                     (unsigned)CTRL_PAD_ACTION_PAD(ev->value),
                     CTRL_PAD_ACTION_SHIFTED(ev->value) ? 1u : 0u);
        }
        return true;

    default:
        return false;
    }
}

static void on_jog(uint8_t deck, uint8_t control, int16_t delta)
{
    deck_state_t *state = &s_decks[normalize_deck(deck)];
    bool touched = deck < DECK_CORE_DECK_COUNT && s_jog_touched[deck];

#if CONFIG_AUDIO_SCRATCH_ENABLED
    if (touched && s_jog_scratch_active[deck] &&
        control == CTRL_DECK_CTL_JOG_SCRATCH) {
        // Scratch (vinyl mode Phase 4): the jog drives the read-head velocity;
        // the audio engine renders the deck from the capture buffer at the head.
        if (deck_uses_audio_engine(deck)) {
            audio_engine_deck_scratch_move(deck, delta);
        }
        ESP_LOGD(TAG, "deck %u scratch move %+d", (unsigned)deck + 1, delta);
        return;
    }
#endif

    if (state->playing && (!touched || control == CTRL_DECK_CTL_JOG_BEND)) {
        /* JOG_BEND is explicitly the platter side-ring / vinyl-off stream and
         * must remain a tempo nudge even if the platter top is also touched.
         * A scratch-stream event without a matching touch is safely treated as
         * bend rather than entering scratch from stale/reordered touch state. */
        if (deck_uses_audio_engine(deck)) {
            audio_engine_deck_jog_nudge(deck, delta);
        }
        ESP_LOGD(TAG, "deck %u jog nudge %+d control=%u", (unsigned)deck + 1,
                 delta, (unsigned)control);
        return;
    }

    // Scrub: advance/rewind the position. Used while paused, and (Phase 1 build)
    // while the platter is held during playback — audio is muted + frozen by the
    // hold, so scrubbing drags the playhead; release resumes.
    int32_t pos = (int32_t)state->position_ms + delta * 3;
    state->position_ms = (pos < 0) ? 0 : (uint32_t)pos;
    if (deck_uses_audio_engine(deck)) {
        audio_engine_deck_seek(deck, state->position_ms);
    }
    ESP_LOGD(TAG, "deck %u jog scrub -> %lu ms", (unsigned)deck + 1,
             (unsigned long)state->position_ms);
}

static void on_jog_search(uint8_t deck, int16_t delta)
{
    if (delta == 0) {
        return;
    }

    deck_state_t *state = &s_decks[normalize_deck(deck)];
    bool uses_audio = deck_uses_audio_engine(deck);
    uint32_t current = uses_audio ? audio_engine_deck_position_ms(deck) : state->position_ms;
    int64_t target = (int64_t)current + ((int64_t)delta * (int64_t)JOG_SEARCH_STEP_MS);
    if (target < 0) {
        target = 0;
    }

    esp_err_t rc = uses_audio ? audio_engine_deck_seek(deck, (uint32_t)target) : ESP_OK;
    if (rc == ESP_OK) {
        state->position_ms = (uint32_t)target;
        ESP_LOGD(TAG, "deck %u jog search %+d -> %lu ms",
                 (unsigned)deck + 1,
                 (int)delta,
                 (unsigned long)state->position_ms);
    } else {
        ESP_LOGW(TAG, "deck %u jog search failed: %s",
                 (unsigned)deck + 1,
                 esp_err_to_name(rc));
    }
}

static void on_browse_event(uint8_t id, int16_t delta)
{
    if (delta == 0) return;
    (void)enqueue_ui_command(&(deck_ui_command_t) {
        .kind = DECK_UI_CMD_BROWSE_DELTA,
        .id = id,
        .value = delta,
    });
}

static void on_browse_press(void)
{
    (void)enqueue_ui_command(&(deck_ui_command_t) {
        .kind = DECK_UI_CMD_TOGGLE_LIBRARY_VIEW,
    });
}

static void on_browse_shift_press(void)
{
    (void)enqueue_ui_command(&(deck_ui_command_t) {
        .kind = DECK_UI_CMD_SHOW_LIBRARY,
    });
}

static void on_pitch(uint8_t deck, int16_t raw)
{
    deck_state_t *state = &s_decks[normalize_deck(deck)];
    bool sync_was_enabled = state->sync_enabled;
    state->sync_enabled = false;
    state->pitch = raw;
    apply_deck_pitch(deck, state);
    int pitch_abs = abs(state->pitch_centipercent);
    ESP_LOGD(TAG, "deck %u pitch %d range ±%u%% effective %c%d.%02d%%",
             (unsigned)deck + 1,
             raw,
             (unsigned)state->tempo_range_percent,
             state->pitch_centipercent >= 0 ? '+' : '-',
             pitch_abs / 100,
             pitch_abs % 100);
    if (sync_was_enabled) {
        publish_flx4_led_snapshot(false);
    }
}

static void apply_deck_pitch(uint8_t deck, deck_state_t *state)
{
    if (!state) {
        return;
    }
    state->pitch_centipercent = tempo_centipercent_from_raw(state->pitch,
                                                           state->tempo_range_percent);
    if (deck_uses_audio_engine(deck)) {
        audio_engine_deck_set_pitch_percent(deck, deck_core_pitch_percent(state));
    }
}

static bool apply_beat_sync(uint8_t deck, deck_state_t *state)
{
    if (!state || deck >= DECK_CORE_DECK_COUNT) {
        return false;
    }

    uint8_t reference_deck = beat_sync_reference_deck(deck);
    int16_t target_centipercent = centipercent_for_bpm_match(deck, reference_deck);
    bool changed = state->pitch_centipercent != target_centipercent || !state->sync_enabled;

    state->sync_enabled = true;
    state->pitch_centipercent = target_centipercent;
    if (deck_uses_audio_engine(deck)) {
        audio_engine_deck_set_pitch_percent(deck, deck_core_pitch_percent(state));
    }

    bool phase_aligned = false;
    uint32_t aligned_ms = current_deck_position_ms(deck, state);
    deck_loaded_track_summary_t target_loaded = {0};
    deck_loaded_track_summary_t reference_loaded = {0};
    anlz_snapshot_t *target_snapshot = NULL;
    anlz_snapshot_t *reference_snapshot = NULL;
    bool target_valid = acquire_loaded_track_for_deck(
        deck, &target_loaded, &target_snapshot);
    bool reference_valid = acquire_loaded_track_for_deck(
        reference_deck, &reference_loaded, &reference_snapshot);
    const anlz_metadata_t *target_meta =
        anlz_snapshot_metadata(target_snapshot);
    const anlz_metadata_t *reference_meta =
        anlz_snapshot_metadata(reference_snapshot);
    uint32_t reference_position_ms = current_deck_position_ms(reference_deck,
                                                             &s_decks[reference_deck]);
    bool phase_target_available = beat_phase_align_target_ms(aligned_ms,
                                                             target_valid &&
                                                                     target_loaded.has_anlz
                                                                 ? target_meta
                                                                 : NULL,
                                                             reference_position_ms,
                                                             reference_valid &&
                                                                     reference_loaded.has_anlz
                                                                 ? reference_meta
                                                                 : NULL,
                                                             &aligned_ms);
    anlz_snapshot_release(target_snapshot);
    anlz_snapshot_release(reference_snapshot);
    if (phase_target_available) {
        esp_err_t seek_rc = audio_engine_deck_seek(deck, aligned_ms);
        if (seek_rc == ESP_OK) {
            state->position_ms = aligned_ms;
            phase_aligned = true;
        } else {
            ESP_LOGW(TAG, "deck %u beat sync phase-align seek failed: %s",
                     (unsigned)deck + 1,
                     esp_err_to_name(seek_rc));
        }
    }

    uint16_t base = deck_base_bpm(deck);
    float reference_bpm = deck_effective_bpm(reference_deck, &s_decks[reference_deck]);
    float synced_bpm = deck_synced_bpm_after_pitch(deck, state->pitch_centipercent);
    bool sync_clamped = beat_sync_requires_clamp(deck, reference_deck, state->pitch_centipercent);
    (void)phase_aligned;
    ESP_LOGI(TAG, "deck %u beat sync target %.2f BPM from deck %u, base %u BPM, actual %.2f BPM, pitch %c%d.%02d%%%s, phase %s%lu ms",
             (unsigned)deck + 1,
             (double)reference_bpm,
             (unsigned)reference_deck + 1,
             (unsigned)base,
             (double)synced_bpm,
             state->pitch_centipercent >= 0 ? '+' : '-',
             abs(state->pitch_centipercent) / 100,
             abs(state->pitch_centipercent) % 100,
             sync_clamped ? " (clamped)" : "",
             phase_aligned ? "aligned -> " : "unchanged @ ",
             (unsigned long)aligned_ms);
    return changed;
}

static uint8_t beat_sync_reference_deck(uint8_t deck)
{
    if (s_sync_master_deck < DECK_CORE_DECK_COUNT && s_sync_master_deck != deck) {
        return s_sync_master_deck;
    }
    return deck == CTRL_DECK_1 ? CTRL_DECK_2 : CTRL_DECK_1;
}

static void set_sync_master(uint8_t deck, deck_state_t *state)
{
    if (deck >= DECK_CORE_DECK_COUNT || !state) {
        return;
    }

    s_sync_master_deck = deck;
    for (uint8_t i = 0; i < DECK_CORE_DECK_COUNT; i++) {
        s_decks[i].sync_master = i == deck;
    }
    state->sync_enabled = false;
    ESP_LOGI(TAG, "deck %u sync master", (unsigned)deck + 1);
    publish_flx4_led_snapshot(false);
}

static void on_mixer_control(uint8_t id, int16_t raw)
{
    uint16_t value = raw < 0 ? 0u : (uint16_t)raw;

    switch (id) {
    case CTRL_ID_CH1_VOLUME:
        audio_engine_set_channel_volume(CTRL_DECK_1, value);
        break;
    case CTRL_ID_CH2_VOLUME:
        audio_engine_set_channel_volume(CTRL_DECK_2, value);
        break;
    case CTRL_ID_CROSSFADER:
        audio_engine_set_crossfader(value);
        break;
    case CTRL_ID_DECK1_PFL:
        if (raw != 0) {
            audio_engine_toggle_pfl(CTRL_DECK_1);
        }
        break;
    case CTRL_ID_DECK2_PFL:
        if (raw != 0) {
            audio_engine_toggle_pfl(CTRL_DECK_2);
        }
        break;
    case CTRL_ID_CH1_EQ_HIGH:
        audio_engine_set_eq(CTRL_DECK_1, AUDIO_EQ_BAND_HIGH, value);
        break;
    case CTRL_ID_CH2_EQ_HIGH:
        audio_engine_set_eq(CTRL_DECK_2, AUDIO_EQ_BAND_HIGH, value);
        break;
    case CTRL_ID_CH1_EQ_MID:
        audio_engine_set_eq(CTRL_DECK_1, AUDIO_EQ_BAND_MID, value);
        break;
    case CTRL_ID_CH2_EQ_MID:
        audio_engine_set_eq(CTRL_DECK_2, AUDIO_EQ_BAND_MID, value);
        break;
    case CTRL_ID_CH1_EQ_LOW:
        audio_engine_set_eq(CTRL_DECK_1, AUDIO_EQ_BAND_LOW, value);
        break;
    case CTRL_ID_CH2_EQ_LOW:
        audio_engine_set_eq(CTRL_DECK_2, AUDIO_EQ_BAND_LOW, value);
        break;
    case CTRL_ID_CH1_FILTER:
        audio_engine_set_filter(CTRL_DECK_1, value);
        break;
    case CTRL_ID_CH2_FILTER:
        audio_engine_set_filter(CTRL_DECK_2, value);
        break;
    case CTRL_ID_CH1_TRIM:
        audio_engine_set_pregain(CTRL_DECK_1, value);
        break;
    case CTRL_ID_CH2_TRIM:
        audio_engine_set_pregain(CTRL_DECK_2, value);
        break;
    case CTRL_ID_MASTER_VOLUME:
        audio_engine_set_master_volume(value);
        break;
    case CTRL_ID_HEADPHONE_MIX:
        audio_engine_set_headphone_mix(value);
        break;
    default:
        break;
    }
}

static bool event_uses_ui_without_deck_state(const ctrl_event_t *ev)
{
    if (control_link_id_is_deck(ev->id)) {
        return false;
    }
    if (ev->type == CTRL_EV_BROWSE) {
        return true;
    }
    if (ev->type != CTRL_EV_BUTTON || ev->value == 0) {
        return false;
    }
    return ev->id == BTN_LOAD ||
           ev->id == BTN_TRACK_PREV ||
           ev->id == BTN_TRACK_NEXT ||
           ev->id == CTRL_ID_BROWSE_PRESS ||
           ev->id == CTRL_ID_BROWSE_SHIFT_PRESS;
}

// ─── Main task ────────────────────────────────────────────────────────────────

static void deck_task(void *arg)
{
    ctrl_event_t ev;
    while (1) {
        publish_state_snapshot();
        if (xQueueReceive(s_queue, &ev, portMAX_DELAY) != pdTRUE) continue;

        if (ev.type == CTRL_EV_STATE && ev.id == DECK_CORE_INTERNAL_RESET_ID) {
            const uint8_t idx = normalize_deck(ev.deck);
            init_deck_state(&s_decks[idx]);
            s_jog_touched[idx] = false;
            s_jog_hold_active[idx] = false;
            s_jog_scratch_active[idx] = false;
            if (s_sync_master_deck == idx) s_sync_master_deck = CTRL_DECK_NONE;
            memset(&s_loop_shadow[idx], 0, sizeof(s_loop_shadow[idx]));
            memset(&s_shifted_loop_roll[idx], 0, sizeof(s_shifted_loop_roll[idx]));
            memset(&s_pad_fx_led[idx], 0, sizeof(s_pad_fx_led[idx]));
            memset(&s_beat_loop_led[idx], 0, sizeof(s_beat_loop_led[idx]));
            memset(&s_censor_shadow[idx], 0, sizeof(s_censor_shadow[idx]));
            hot_cue_mask_cache_invalidate(idx);
            publish_state_snapshot();
            if (s_reset_done_sem) xSemaphoreGive(s_reset_done_sem);
            continue;
        }

        if (event_uses_ui_without_deck_state(&ev)) {
            if (ev.type == CTRL_EV_BROWSE) {
                on_browse_event(ev.id, ev.value);
            } else if (ev.id == CTRL_ID_BROWSE_PRESS) {
                on_browse_press();
            } else if (ev.id == CTRL_ID_BROWSE_SHIFT_PRESS) {
                if (ev.value != 0) on_browse_shift_press();
            } else {
                on_button(DECK_CORE_COMPAT_DECK, button_for_event(&ev), ev.value != 0);
            }
            continue;
        }

        if (ev.type == CTRL_EV_STATE) {
            on_state_event(&ev);
            continue;
        }

        if (on_system_button(&ev)) {
            continue;
        }

        if (on_system_value(&ev)) {
            continue;
        }

        uint8_t deck = deck_index_for_event(&ev);

        if (event_is_mixer_control(&ev)) {
            on_mixer_control(ev.id, ev.value);
            continue;
        }

        if (on_deck_extension_button(&ev)) {
            continue;
        }

        switch (ev.type) {
        case CTRL_EV_BUTTON:
            on_button(deck, button_for_event(&ev), ev.value != 0);
            break;
        case CTRL_EV_JOG:
            if (control_link_id_control(ev.id) == CTRL_DECK_CTL_LOOP_SIZE) {
                on_loop_size_delta(deck, ev.value, &s_decks[deck]);
            } else if (control_link_id_control(ev.id) == CTRL_DECK_CTL_JOG_SEARCH) {
                on_jog_search(deck, ev.value);
            } else {
                on_jog(deck, control_link_id_control(ev.id), ev.value);
            }
            break;
        case CTRL_EV_BROWSE:
            on_browse_event(ev.id, ev.value);
            break;
        case CTRL_EV_PITCH:
            on_pitch(deck, ev.value);
            break;
        case CTRL_EV_HEARTBEAT:
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            s_last_heartbeat_tick = xTaskGetTickCount();
            xSemaphoreGive(s_mutex);
            ESP_LOGD(TAG, "S3 heartbeat seq=%d", ev.seq);
            break;
        case CTRL_EV_STATE:
            on_state_event(&ev);
            break;
        }
    }
}

// ─── Public API ───────────────────────────────────────────────────────────────

esp_err_t deck_core_init(QueueHandle_t *ctrl_event_queue_out)
{
    deck_loaded_track_store_reset(&s_loaded_tracks);
    for (uint8_t i = 0; i < DECK_CORE_DECK_COUNT; i++) {
        init_deck_state(&s_decks[i]);
    }
    init_beat_fx_state();
    flx4_led_publisher_init(&s_flx4_led_publisher);

    s_mutex = xSemaphoreCreateMutex();
    s_reset_done_sem = xSemaphoreCreateBinary();
    if (!s_mutex || !s_reset_done_sem) return ESP_ERR_NO_MEM;
    publish_state_snapshot();

    s_queue = xQueueCreate(CTRL_QUEUE_LEN, sizeof(ctrl_event_t));
    if (!s_queue) {
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_ui_command_queue = xQueueCreate(DECK_UI_COMMAND_QUEUE_LEN, sizeof(deck_ui_command_t));
    if (!s_ui_command_queue) {
        vQueueDelete(s_queue);
        s_queue = NULL;
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(deck_task, "deck", DECK_TASK_STACK_BYTES, NULL, 5,
#if !defined(DECK_CORE_PC_TEST)
                    &s_deck_task
#else
                    NULL
#endif
                    ) != pdPASS) {
        deck_core_cleanup_init_failure();
        return ESP_ERR_NO_MEM;
    }

#if !defined(DECK_CORE_PC_TEST)
    s_vu_meters_active = false;
    if (!s_vu_task) {
        if (xTaskCreate(vu_task, "flx4_vu", 3072, NULL, 3, &s_vu_task) != pdPASS) {
            deck_core_cleanup_init_failure();
            return ESP_ERR_NO_MEM;
        }
    }
#endif

    *ctrl_event_queue_out = s_queue;
    ESP_LOGI(TAG, "deck core ready");
    return ESP_OK;
}

void deck_core_process_ui_commands(void)
{
    /*
     * ui_update() is the sole firmware caller. Controller input only enqueues
     * compact commands on the deck task, so table navigation, screen switching
     * and track-load submission all execute in the LVGL task context.
     *
     * Bound the drain so an encoder burst cannot monopolize one display frame.
     */
    for (size_t processed = 0u;
         processed < DECK_UI_COMMANDS_PER_FRAME;
         processed++) {
        deck_ui_command_t cmd;
#if defined(DECK_CORE_PC_TEST)
        if (s_test_ui_command_count == 0u) break;
        cmd = s_test_ui_commands[0];
        if (s_test_ui_command_count > 1u) {
            memmove(&s_test_ui_commands[0],
                    &s_test_ui_commands[1],
                    (s_test_ui_command_count - 1u) *
                        sizeof(s_test_ui_commands[0]));
        }
        s_test_ui_command_count--;
#else
        if (!s_ui_command_queue ||
            xQueueReceive(s_ui_command_queue, &cmd, 0) != pdTRUE) {
            break;
        }
#endif
        execute_ui_command(&cmd);
    }
}

static deck_core_activity_cb_t s_activity_cb = NULL;

void deck_core_set_activity_cb(deck_core_activity_cb_t cb)
{
    s_activity_cb = cb;
}

esp_err_t deck_core_queue_event(const ctrl_event_t *ev)
{
    if (!s_queue || !ev) return ESP_ERR_INVALID_ARG;
    /* Single choke point for every FLX4 button, jog, fader and web mutation.
     * Swallowing the waking event here is what stops a PLAY press that
     * dismisses the screensaver from also starting the deck. */
    if (s_activity_cb && s_activity_cb()) return ESP_OK;
    if (xQueueSend(s_queue, ev, 0) != pdTRUE) {
        s_drop_count++;
        TickType_t now = xTaskGetTickCount();
        if (now - s_last_drop_warn >= pdMS_TO_TICKS(1000)) {
            s_last_drop_warn = now;
            ESP_LOGW(TAG, "queue full, drops=%" PRIu32 " last_type=%d",
                     s_drop_count, ev->type);
        }
        return ESP_FAIL;
    }
    return ESP_OK;
}

deck_state_t deck_core_get_state(void)
{
    return deck_core_get_deck_state(DECK_CORE_COMPAT_DECK);
}

void deck_core_toggle_master_tempo(uint8_t deck)
{
    if (!s_queue || deck >= DECK_CORE_DECK_COUNT) return;
    ctrl_event_t ev = {
        .type = CTRL_EV_BUTTON,
        .id = BTN_MASTER_TEMPO,
        .value = 1,
        .deck = deck,
    };
    if (xQueueSend(s_queue, &ev, 0) != pdTRUE) {
        ESP_LOGW(TAG, "deck %u master tempo UI event dropped", (unsigned)deck + 1u);
    }
}

static void publish_loaded_track_hot_cue_leds(uint8_t deck)
{
    if (deck >= DECK_CORE_DECK_COUNT) return;
    deck_state_t state = deck_core_get_deck_state(deck);
    uint8_t mask = hot_cue_exists_mask_for_deck(deck);
    for (uint8_t pad = 0; pad < HOT_CUE_STORE_SLOT_COUNT; pad++) {
        uint8_t value = (state.pad_mode == CTRL_PAD_MODE_HOT_CUE &&
                         (mask & (1u << pad)) != 0u) ? 1u : 0u;
        uint8_t previous = (s_loaded_hot_cue_mask[deck] & (1u << pad)) != 0u ? 1u : 0u;
        /* The FLX4 visibly sweeps a pad bank when it receives eight sequential
         * OFF notes. On the first load there is no prior illuminated track to
         * clear, so emit only existing cues. Later loads emit a true mask diff
         * and still clear cues that belonged to the previous track. */
        if ((!s_loaded_hot_cue_mask_valid[deck] && value != 0u) ||
            (s_loaded_hot_cue_mask_valid[deck] && value != previous)) {
            deck_send_led((led_id_t)(LED_HOT_CUE_PAD_1 + pad), value, deck);
        }
    }
    s_loaded_hot_cue_mask[deck] =
        state.pad_mode == CTRL_PAD_MODE_HOT_CUE ? mask : 0u;
    s_loaded_hot_cue_mask_valid[deck] = true;
}

deck_state_t deck_core_get_deck_state(uint8_t deck)
{
    const uint8_t idx = normalize_deck(deck);
    deck_state_t snap = {0};
    uint32_t heartbeat_tick = 0u;
    copy_state_snapshot(idx, &snap, NULL, NULL, &heartbeat_tick);

    if (deck_uses_audio_engine(idx)) {
        snap.playing = audio_engine_deck_is_playing(idx);
        snap.position_ms = audio_engine_deck_position_ms(idx);
    }
    const TickType_t now = xTaskGetTickCount();
    if (heartbeat_tick != 0u) {
        const uint32_t age_ms = (uint32_t)((now - (TickType_t)heartbeat_tick) * portTICK_PERIOD_MS);
        snap.last_heartbeat_age_ms = age_ms;
        snap.control_link_connected = age_ms <= 10000u;
    } else {
        snap.last_heartbeat_age_ms = UINT32_MAX;
        snap.control_link_connected = false;
    }
    return snap;
}

deck_core_beat_fx_state_t deck_core_get_beat_fx_state(void)
{
    deck_core_beat_fx_state_t snap = {0};
    copy_state_snapshot(0u, NULL, NULL, &snap, NULL);
    return snap;
}

void deck_core_set_s3_debug_ap_status_cb(deck_core_s3_debug_ap_status_cb_t cb)
{
    s_s3_debug_ap_status_cb = cb;
}

void deck_core_set_s3_debug_ap_token_cb(deck_core_s3_debug_ap_token_cb_t cb)
{
    s_s3_debug_ap_token_cb = cb;
}

deck_core_loop_display_t deck_core_get_loop_display(uint8_t deck)
{
    deck_core_loop_display_t out = {0};
    if (deck >= DECK_CORE_DECK_COUNT) return out;

    deck_loop_shadow_t shadow = {0};
    copy_state_snapshot(deck, NULL, &shadow, NULL, NULL);
    bool active = false;
    uint32_t start_ms = 0u;
    uint32_t end_ms = 0u;
    if (read_active_loop(deck, &active, &start_ms, &end_ms) && active && end_ms > start_ms) {
        out.active = true;
        out.start_ms = start_ms;
        out.end_ms = end_ms;
    } else if (shadow.pending_in) {
        out.armed = true;
        out.start_ms = shadow.pending_start_ms;
    }
    return out;
}

void deck_core_reset(void)
{
    deck_core_reset_deck(DECK_CORE_COMPAT_DECK);
}

void deck_core_reset_deck(uint8_t deck)
{
#if defined(DECK_CORE_PC_TEST)
    const uint8_t idx = normalize_deck(deck);
    init_deck_state(&s_decks[idx]);
    s_jog_touched[idx] = false;
    s_jog_hold_active[idx] = false;
    s_jog_scratch_active[idx] = false;
    if (s_sync_master_deck == idx) s_sync_master_deck = CTRL_DECK_NONE;
    memset(&s_loop_shadow[idx], 0, sizeof(s_loop_shadow[idx]));
    memset(&s_shifted_loop_roll[idx], 0, sizeof(s_shifted_loop_roll[idx]));
    memset(&s_pad_fx_led[idx], 0, sizeof(s_pad_fx_led[idx]));
    memset(&s_beat_loop_led[idx], 0, sizeof(s_beat_loop_led[idx]));
    memset(&s_censor_shadow[idx], 0, sizeof(s_censor_shadow[idx]));
    hot_cue_mask_cache_invalidate(idx);
    publish_state_snapshot();
#else
    if (!s_queue || !s_reset_done_sem) return;
    while (xSemaphoreTake(s_reset_done_sem, 0) == pdTRUE) {}
    ctrl_event_t ev = {
        .type = CTRL_EV_STATE,
        .id = DECK_CORE_INTERNAL_RESET_ID,
        .deck = normalize_deck(deck),
    };
    if (xQueueSend(s_queue, &ev, portMAX_DELAY) != pdTRUE ||
        xSemaphoreTake(s_reset_done_sem, pdMS_TO_TICKS(DECK_CORE_RESET_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "deck %u actor reset timed out", (unsigned)ev.deck + 1u);
        return;
    }
    ESP_LOGI(TAG, "deck %u core reset", (unsigned)ev.deck + 1u);
#endif
}

static esp_err_t loaded_track_result_to_esp(
    deck_loaded_track_result_t result)
{
    switch (result) {
    case DECK_LOADED_TRACK_OK:
        return ESP_OK;
    case DECK_LOADED_TRACK_INVALID:
        return ESP_ERR_INVALID_ARG;
    case DECK_LOADED_TRACK_STALE:
        return ESP_ERR_INVALID_STATE;
    case DECK_LOADED_TRACK_NO_MEMORY:
        return ESP_ERR_NO_MEM;
    default:
        return ESP_FAIL;
    }
}

esp_err_t deck_core_publish_loaded_track(uint8_t deck,
                                         uint32_t media_generation,
                                         uint32_t track_key,
                                         uint16_t bpm,
                                         uint32_t duration_ms,
                                         const anlz_metadata_t *anlz)
{
    const deck_loaded_track_payload_t payload = {
        .media_generation = media_generation,
        .track_key = track_key,
        .duration_ms = duration_ms,
        .bpm = bpm,
        .anlz = anlz,
    };
    return loaded_track_result_to_esp(deck_loaded_track_store_publish(
        &s_loaded_tracks, deck, &payload));
}

esp_err_t deck_core_clear_loaded_track(uint8_t deck,
                                       uint32_t media_generation)
{
    return loaded_track_result_to_esp(deck_loaded_track_store_clear(
        &s_loaded_tracks, deck, media_generation));
}

esp_err_t deck_core_clear_loaded_tracks(uint32_t media_generation)
{
    return loaded_track_result_to_esp(deck_loaded_track_store_clear_all(
        &s_loaded_tracks, media_generation));
}

bool deck_core_get_loaded_track(uint8_t deck,
                                deck_loaded_track_summary_t *out)
{
    return deck_loaded_track_store_get(&s_loaded_tracks, deck, out);
}

#if defined(DECK_CORE_PC_TEST)
void deck_core_test_reset(void)
{
    deck_loaded_track_store_reset(&s_loaded_tracks);
    for (uint8_t i = 0; i < DECK_CORE_DECK_COUNT; i++) {
        init_deck_state(&s_decks[i]);
    }
    s_sync_master_deck = CTRL_DECK_NONE;
    init_beat_fx_state();
    memset(s_loop_shadow, 0, sizeof(s_loop_shadow));
    memset(s_shifted_loop_roll, 0, sizeof(s_shifted_loop_roll));
    memset(s_pad_fx_led, 0, sizeof(s_pad_fx_led));
    memset(s_beat_loop_led, 0, sizeof(s_beat_loop_led));
    memset(s_censor_shadow, 0, sizeof(s_censor_shadow));
    memset(s_track_load_led_valid, 0, sizeof(s_track_load_led_valid));
    memset(s_track_load_led_state, 0, sizeof(s_track_load_led_state));
    memset(s_loaded_hot_cue_mask_valid, 0, sizeof(s_loaded_hot_cue_mask_valid));
    memset(s_loaded_hot_cue_mask, 0, sizeof(s_loaded_hot_cue_mask));
    memset(s_beat_jump_pad_led_valid, 0, sizeof(s_beat_jump_pad_led_valid));
    memset(s_beat_jump_pad_led_state, 0, sizeof(s_beat_jump_pad_led_state));
    memset(s_deck_shift_held, 0, sizeof(s_deck_shift_held));
    memset(s_jog_touched, 0, sizeof(s_jog_touched));
    memset(s_jog_hold_active, 0, sizeof(s_jog_hold_active));
    memset(s_jog_scratch_active, 0, sizeof(s_jog_scratch_active));
    memset(s_beat_jump_shift_helper_led_valid, 0, sizeof(s_beat_jump_shift_helper_led_valid));
    memset(s_beat_jump_shift_helper_led_state, 0, sizeof(s_beat_jump_shift_helper_led_state));
    memset(s_hot_cue_mask_cache_key, 0, sizeof(s_hot_cue_mask_cache_key));
    memset(s_hot_cue_mask_cache_value, 0, sizeof(s_hot_cue_mask_cache_value));
#if defined(DECK_CORE_PC_TEST)
    memset(s_deferred_mixer_last, 0, sizeof(s_deferred_mixer_last));
    memset(s_deferred_mixer_seen, 0, sizeof(s_deferred_mixer_seen));
#endif
    flx4_led_publisher_init(&s_flx4_led_publisher);
    s_last_heartbeat_tick = 0;
    s_flx4_connection_state_valid = false;
    s_flx4_connected = false;
    s_test_ui_command_count = 0;
}

void deck_core_test_flush_ui_commands(void)
{
    while (s_test_ui_command_count > 0) {
        deck_core_process_ui_commands();
    }
}

void deck_core_test_apply_event(const ctrl_event_t *ev)
{
    if (!ev) return;

    if (event_uses_ui_without_deck_state(ev)) {
        if (ev->type == CTRL_EV_BROWSE) {
            on_browse_event(ev->id, ev->value);
        } else if (ev->id == CTRL_ID_BROWSE_PRESS) {
            on_browse_press();
        } else if (ev->id == CTRL_ID_BROWSE_SHIFT_PRESS) {
            if (ev->value != 0) on_browse_shift_press();
        } else {
            on_button(DECK_CORE_COMPAT_DECK, button_for_event(ev), ev->value != 0);
        }
        return;
    }

    if (ev->type == CTRL_EV_STATE) {
        on_state_event(ev);
        return;
    }

    if (on_system_button(ev)) {
        return;
    }

    if (on_system_value(ev)) {
        return;
    }

    uint8_t deck = deck_index_for_event(ev);

    if (event_is_mixer_control(ev)) {
        on_mixer_control(ev->id, ev->value);
        return;
    }

    if (on_deck_extension_button(ev)) {
        return;
    }

    switch (ev->type) {
    case CTRL_EV_BUTTON:
        on_button(deck, button_for_event(ev), ev->value != 0);
        break;
    case CTRL_EV_JOG:
        if (control_link_id_control(ev->id) == CTRL_DECK_CTL_LOOP_SIZE) {
            on_loop_size_delta(deck, ev->value, &s_decks[deck]);
        } else if (control_link_id_control(ev->id) == CTRL_DECK_CTL_JOG_SEARCH) {
            on_jog_search(deck, ev->value);
        } else {
            on_jog(deck, control_link_id_control(ev->id), ev->value);
        }
        break;
    case CTRL_EV_BROWSE:
        on_browse_event(ev->id, ev->value);
        break;
    case CTRL_EV_PITCH:
        on_pitch(deck, ev->value);
        break;
    case CTRL_EV_HEARTBEAT:
        s_last_heartbeat_tick = xTaskGetTickCount();
        break;
    case CTRL_EV_STATE:
        on_state_event(ev);
        break;
    }
}

deck_state_t deck_core_test_get_deck_state(uint8_t deck)
{
    uint8_t idx = normalize_deck(deck);
    if (deck_uses_audio_engine(idx)) {
        s_decks[idx].playing = audio_engine_deck_is_playing(idx);
        s_decks[idx].position_ms = audio_engine_deck_position_ms(idx);
    }
    return s_decks[idx];
}

deck_core_beat_fx_state_t deck_core_test_get_beat_fx_state(void)
{
    return deck_core_get_beat_fx_state();
}

bool deck_core_test_should_log_deferred_mixer_value(uint8_t id, uint16_t value)
{
    return should_log_deferred_mixer_value(id, value);
}

bool deck_core_test_should_log_deferred_button(uint8_t id, int16_t value)
{
    return should_log_deferred_button(id, value);
}
#endif
