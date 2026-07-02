#include "deck_core.h"
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
#define BEAT_FX_ECHO_FALLBACK_BPM 120.0f
#define BEAT_FX_ECHO_MIN_BPM 40.0f
#define BEAT_FX_ECHO_MAX_BPM 300.0f
#define BEAT_FX_ECHO_MAX_DELAY_MS 1000u

extern bool ui_is_library_active(void) __attribute__((weak));
extern bool ui_is_overview_active(void) __attribute__((weak));
extern esp_err_t ui_show_library(void) __attribute__((weak));
extern esp_err_t ui_toggle_library_view(void) __attribute__((weak));
extern esp_err_t ui_library_select_delta(int delta) __attribute__((weak));
extern esp_err_t ui_overview_zoom_delta(int delta) __attribute__((weak));
extern esp_err_t ui_library_load_selected(void) __attribute__((weak));
extern esp_err_t ui_library_load_selected_for_deck(uint8_t deck) __attribute__((weak));
extern uint32_t ui_library_loaded_track_key_for_deck(uint8_t deck) __attribute__((weak));
extern const anlz_metadata_t *ui_get_deck_anlz_metadata(uint8_t deck) __attribute__((weak));
extern uint16_t ui_library_deck_bpm(uint8_t deck, uint16_t fallback_bpm) __attribute__((weak));

static QueueHandle_t    s_queue;
static SemaphoreHandle_t s_mutex;
static deck_state_t     s_decks[DECK_CORE_DECK_COUNT];
static flx4_led_publisher_t s_flx4_led_publisher;
static deck_core_beat_fx_state_t s_beat_fx;
static uint32_t          s_drop_count;
static TickType_t        s_last_drop_warn;
static TickType_t        s_last_heartbeat_tick;
static bool              s_flx4_connection_state_valid;
static bool              s_flx4_connected;
static uint8_t           s_sync_master_deck = CTRL_DECK_NONE;
#if !defined(DECK_CORE_PC_TEST)
static esp_timer_handle_t s_vu_timer;
#endif
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

typedef struct {
    bool active;
    bool previous_active;
    uint32_t previous_start_ms;
    uint32_t previous_end_ms;
} deck_shifted_loop_roll_t;

static deck_shifted_loop_roll_t s_shifted_loop_roll[DECK_CORE_DECK_COUNT];

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

#define DECK_CORE_DEFERRED_MIXER_LOG_STEP 2048u

static void publish_flx4_led_snapshot(bool force);
static void apply_deck_pitch(uint8_t deck, deck_state_t *state);
static bool apply_beat_sync(uint8_t deck, deck_state_t *state);
static uint8_t beat_sync_reference_deck(uint8_t deck);
static void set_sync_master(uint8_t deck, deck_state_t *state);
static const anlz_metadata_t *loaded_anlz_for_deck(uint8_t deck);
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

static void sync_beat_fx_audio_state(void)
{
    audio_engine_beat_fx_target_t target = beat_fx_audio_target(s_beat_fx.target);
    bool filter_enabled = s_beat_fx.enabled &&
                          s_beat_fx.effect == DECK_CORE_BEAT_FX_FILTER;
    bool echo_enabled = s_beat_fx.enabled &&
                        s_beat_fx.effect == DECK_CORE_BEAT_FX_ECHO;

    audio_engine_set_beat_fx_filter(target,
                                    s_beat_fx.depth,
                                    filter_enabled);
    audio_engine_set_beat_fx_echo(target,
                                  s_beat_fx.depth,
                                  beat_fx_delay_ms(s_beat_fx.beat, s_beat_fx.target),
                                  echo_enabled);
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
    const anlz_metadata_t *meta = loaded_anlz_for_deck(deck);
    if (meta && meta->beats && meta->beat_count > 0 && meta->beats[0].bpm_x100 > 0) {
        return (uint16_t)((meta->beats[0].bpm_x100 + 50u) / 100u);
    }
    if (ui_library_deck_bpm) {
        uint16_t bpm = ui_library_deck_bpm(deck, 120);
        return bpm > 0 ? bpm : 120;
    }
    return 120;
}

static uint32_t deck_base_bpm_x100(uint8_t deck)
{
    const anlz_metadata_t *meta = loaded_anlz_for_deck(deck);
    if (meta && meta->beats && meta->beat_count > 0 && meta->beats[0].bpm_x100 > 0) {
        return meta->beats[0].bpm_x100;
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
    if (deck >= DECK_CORE_DECK_COUNT || !ui_library_loaded_track_key_for_deck) {
        return 0;
    }
    return ui_library_loaded_track_key_for_deck(deck);
}

static uint8_t hot_cue_exists_mask_for_deck(uint8_t deck)
{
    uint32_t track_key = loaded_track_key_for_deck(deck);
    if (track_key == 0) {
        return 0;
    }

    hot_cue_store_blob_t blob = {0};
    if (hot_cue_store_load(track_key, &blob) != ESP_OK) {
        return 0;
    }
    return (uint8_t)(blob.valid_mask & 0xFFu);
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

static const anlz_metadata_t *loaded_anlz_for_deck(uint8_t deck)
{
    if (deck >= DECK_CORE_DECK_COUNT || !ui_get_deck_anlz_metadata) {
        return NULL;
    }
    return ui_get_deck_anlz_metadata(deck);
}

static uint16_t loaded_bpm_for_deck(uint8_t deck)
{
    if (deck >= DECK_CORE_DECK_COUNT || !ui_library_deck_bpm) {
        return 120u;
    }
    return ui_library_deck_bpm(deck, 120u);
}

static void handle_beat_jump(uint8_t deck, int beat_shift, deck_state_t *state)
{
    if (deck >= DECK_CORE_DECK_COUNT || !state || beat_shift == 0) {
        return;
    }

    uint32_t position_ms = current_deck_position_ms(deck, state);
    const anlz_metadata_t *meta = loaded_anlz_for_deck(deck);
    uint16_t bpm = loaded_bpm_for_deck(deck);
    uint32_t target_ms = beat_jump_calculate_target_ms(position_ms, bpm, beat_shift, meta);

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
    const anlz_metadata_t *meta = loaded_anlz_for_deck(deck);
    if (!meta || !meta->beats || meta->beat_count == 0) {
        return position_ms;
    }

    uint32_t best_ms = meta->beats[0].time_ms;
    uint32_t best_delta = best_ms > position_ms ? best_ms - position_ms : position_ms - best_ms;
    for (uint16_t i = 1; i < meta->beat_count; i++) {
        uint32_t beat_ms = meta->beats[i].time_ms;
        uint32_t delta = beat_ms > position_ms ? beat_ms - position_ms : position_ms - beat_ms;
        if (delta < best_delta) {
            best_delta = delta;
            best_ms = beat_ms;
        }
    }
    return best_ms;
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
    uint32_t duration_ms = beat_loop_calculate_duration_ms(start_ms,
                                                           loaded_bpm_for_deck(deck),
                                                           length.numerator,
                                                           length.denominator,
                                                           loaded_anlz_for_deck(deck));
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

static void sync_leds(uint8_t deck)
{
    if (deck != DECK_CORE_COMPAT_DECK) return;
    deck_state_t *state = &s_decks[deck];
    control_link_send_led(LED_PLAY, state->playing ? 1 : 0);
    control_link_send_led(LED_CUE,  (state->position_ms == state->cue_point_ms) ? 1 : 0);
}

static esp_err_t send_snapshot_led(led_id_t led, uint8_t state, uint8_t deck, void *ctx)
{
    (void)ctx;
    control_link_send_led_deck(led, state, deck);
    return ESP_OK;
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

    esp_err_t rc = flx4_led_publisher_publish(&s_flx4_led_publisher,
                                              &input,
                                              force,
                                              send_snapshot_led,
                                              NULL);
    if (rc != ESP_OK) {
        ESP_LOGW(TAG, "FLX4 LED snapshot publish failed: %s", esp_err_to_name(rc));
    } else {
        ESP_LOGI(TAG, "%s FLX4 LED snapshot published", force ? "forced" : "diff");
    }
}

static void on_state_event(const ctrl_event_t *ev)
{
    if (!ev) {
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
        control_link_send_led_deck(LED_SMART_CFX,
                                   audio_engine_get_smart_cfx_enabled() ? 1u : 0u,
                                   CTRL_DECK_1);
        return true;
    case CTRL_ID_SMART_FADER:
        if (ev->value == 0) {
            return true;
        }
        audio_engine_toggle_smart_fader();
        control_link_send_led_deck(LED_SMART_FADER,
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
        control_link_send_led_deck(LED_MASTER_CUE,
                                   audio_engine_get_master_cue_enabled() ? 1u : 0u,
                                   CTRL_DECK_1);
        return true;
    case CTRL_ID_BEAT_FX_SELECT_NEXT:
        if (ev->value != 0) {
            s_beat_fx.effect = (deck_core_beat_fx_effect_t)((s_beat_fx.effect + 1) %
                                                            DECK_CORE_BEAT_FX_COUNT);
            sync_beat_fx_audio_state();
            ESP_LOGI(TAG, "beat fx effect -> %d", (int)s_beat_fx.effect);
        }
        return true;
    case CTRL_ID_BEAT_FX_SELECT_PREV:
        if (ev->value != 0) {
            s_beat_fx.effect = s_beat_fx.effect == 0
                ? (deck_core_beat_fx_effect_t)(DECK_CORE_BEAT_FX_COUNT - 1)
                : (deck_core_beat_fx_effect_t)(s_beat_fx.effect - 1);
            sync_beat_fx_audio_state();
            ESP_LOGI(TAG, "beat fx effect -> %d", (int)s_beat_fx.effect);
        }
        return true;
    case CTRL_ID_BEAT_FX_BEAT_DEC:
        if (ev->value != 0 && s_beat_fx.beat > DECK_CORE_BEAT_FX_BEAT_1_4) {
            s_beat_fx.beat = (deck_core_beat_fx_beat_t)(s_beat_fx.beat - 1);
            ESP_LOGI(TAG, "beat fx beat -> %d", (int)s_beat_fx.beat);
        }
        return true;
    case CTRL_ID_BEAT_FX_BEAT_INC:
        if (ev->value != 0 && s_beat_fx.beat < DECK_CORE_BEAT_FX_BEAT_4) {
            s_beat_fx.beat = (deck_core_beat_fx_beat_t)(s_beat_fx.beat + 1);
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
            control_link_send_led_deck(LED_BEAT_FX_ON,
                                       s_beat_fx.enabled ? 1u : 0u,
                                       CTRL_DECK_1);
            ESP_LOGI(TAG, "beat fx -> %s", s_beat_fx.enabled ? "ON" : "OFF");
        }
        return true;
    case CTRL_ID_BEAT_FX_CLEAR:
        if (ev->value != 0) {
            init_beat_fx_state();
            sync_beat_fx_audio_state();
            control_link_send_led_deck(LED_BEAT_FX_ON, 0u, CTRL_DECK_1);
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
        sync_leds(deck);
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
        sync_leds(deck);
        break;

    case BTN_MODE:
        state->perf_mode = (perf_mode_t)((state->perf_mode + 1) % PERF_MODE_COUNT);
        ESP_LOGI(TAG, "deck %u perf mode -> %d", (unsigned)deck + 1, state->perf_mode);
        break;

    case BTN_MASTER_TEMPO:
        state->master_tempo = !state->master_tempo;
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
        sync_leds(deck);
        break;

    case BTN_LOAD:
        if (ui_library_load_selected_for_deck) {
            esp_err_t rc = ui_library_load_selected_for_deck(deck);
            ESP_LOGI(TAG, "deck %u load selected -> %s", (unsigned)deck + 1,
                     esp_err_to_name(rc));
        } else if (deck == DECK_CORE_COMPAT_DECK && ui_library_load_selected) {
            esp_err_t rc = ui_library_load_selected();
            ESP_LOGI(TAG, "load selected -> %s", esp_err_to_name(rc));
        } else {
            ESP_LOGW(TAG, "load selected unsupported: UI API unavailable");
        }
        break;

    case BTN_TRACK_PREV:
    case BTN_TRACK_NEXT:
        if (ui_is_library_active && ui_library_select_delta && ui_is_library_active()) {
            int delta = (btn == BTN_TRACK_NEXT) ? 1 : -1;
            esp_err_t rc = ui_library_select_delta(delta);
            ESP_LOGD(TAG, "track select %+d → %s", delta, esp_err_to_name(rc));
        } else {
            ESP_LOGD(TAG, "track %s pressed outside library tab",
                     btn == BTN_TRACK_NEXT ? "next" : "prev");
        }
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

static void vu_timer_cb(void *arg)
{
    (void)arg;
    for (uint8_t deck = 0; deck < DECK_CORE_DECK_COUNT; deck++) {
        uint8_t level = peak_to_midi_level(audio_engine_get_deck_peak(deck));
        control_link_send_led_deck(LED_VU_METER, level, deck);
    }
}
#endif

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
    case CTRL_DECK_CTL_SHIFT:
        ESP_LOGD(TAG, "deck %u shift -> %s", (unsigned)deck + 1,
                 pressed ? "pressed" : "released");
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
        sync_leds(deck);
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
            return true;
        case CTRL_DECK_EXT_ACTION_LOOP_ADJUST_OUT:
            adjust_loop_boundary(deck, false, state);
            return true;
        case CTRL_DECK_EXT_ACTION_QUANTIZE:
            state->quantize_enabled = !state->quantize_enabled;
            ESP_LOGI(TAG, "deck %u quantize -> %s",
                     (unsigned)deck + 1,
                     state->quantize_enabled ? "ON" : "OFF");
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
            state->pad_mode = CTRL_PAD_MODE_KEYBOARD;
            ESP_LOGI(TAG, "deck %u pad mode -> KEYBOARD (behavior deferred)",
                     (unsigned)deck + 1);
            publish_flx4_led_snapshot(false);
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
            state->perf_mode = PERF_MODE_KEY_SHIFT;
            state->pad_mode = CTRL_PAD_MODE_KEY_SHIFT;
            ESP_LOGI(TAG, "deck %u pad mode -> KEY_SHIFT", (unsigned)deck + 1);
            publish_flx4_led_snapshot(false);
        }
        return true;

    case CTRL_DECK_CTL_PAD_MODE_SAMPLER:
        if (pressed) {
            state->pad_mode = CTRL_PAD_MODE_SAMPLER;
            ESP_LOGI(TAG, "deck %u pad mode -> SAMPLER (behavior deferred)",
                     (unsigned)deck + 1);
            publish_flx4_led_snapshot(false);
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

static void on_jog(uint8_t deck, int16_t delta)
{
    deck_state_t *state = &s_decks[normalize_deck(deck)];
    if (state->playing) {
        // Nudge: shift position slightly (placeholder — audio_engine handles real nudge)
        ESP_LOGD(TAG, "deck %u jog nudge %+d", (unsigned)deck + 1, delta);
    } else {
        // Scratch: advance/rewind position while paused
        int32_t pos = (int32_t)state->position_ms + delta * 3;
        state->position_ms = (pos < 0) ? 0 : (uint32_t)pos;
        if (deck_uses_audio_engine(deck)) {
            audio_engine_deck_seek(deck, state->position_ms);
        }
        ESP_LOGD(TAG, "deck %u jog scratch -> %lu ms", (unsigned)deck + 1,
                 (unsigned long)state->position_ms);
    }
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
    bool shifted = id == CTRL_ID_BROWSE_SHIFT_DELTA;
    bool library_active = !ui_is_library_active || ui_is_library_active();
    bool overview_active = ui_is_overview_active && ui_is_overview_active();
    if (library_active && ui_library_select_delta) {
        int scaled = shifted ? delta * BROWSE_SHIFT_LIBRARY_MULTIPLIER : delta;
        esp_err_t rc = ui_library_select_delta(scaled);
        ESP_LOGD(TAG, "browse %+d -> %s", scaled, esp_err_to_name(rc));
    } else if (!library_active && overview_active && ui_overview_zoom_delta) {
        int scaled = shifted ? delta * BROWSE_SHIFT_OVERVIEW_MULTIPLIER : delta;
        esp_err_t rc = ui_overview_zoom_delta(scaled);
        ESP_LOGD(TAG, "overview zoom %+d -> %s", scaled, esp_err_to_name(rc));
    } else {
        ESP_LOGW(TAG, "browse unsupported: UI API unavailable");
    }
}

static void on_browse_press(void)
{
    if (ui_toggle_library_view) {
        esp_err_t rc = ui_toggle_library_view();
        ESP_LOGD(TAG, "browse press -> library/overview toggle: %s", esp_err_to_name(rc));
    } else if (ui_show_library) {
        esp_err_t rc = ui_show_library();
        ESP_LOGD(TAG, "browse press -> library fallback: %s", esp_err_to_name(rc));
    } else {
        ESP_LOGW(TAG, "browse press unsupported: UI API unavailable");
    }
}

static void on_browse_shift_press(void)
{
    if (ui_show_library) {
        esp_err_t rc = ui_show_library();
        ESP_LOGD(TAG, "browse shift press -> library: %s", esp_err_to_name(rc));
    } else if (ui_toggle_library_view) {
        esp_err_t rc = ui_toggle_library_view();
        ESP_LOGD(TAG, "browse shift press fallback -> toggle: %s", esp_err_to_name(rc));
    } else {
        ESP_LOGW(TAG, "browse shift press unsupported: UI API unavailable");
    }
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
    const anlz_metadata_t *target_meta = loaded_anlz_for_deck(deck);
    const anlz_metadata_t *reference_meta = loaded_anlz_for_deck(reference_deck);
    uint32_t reference_position_ms = current_deck_position_ms(reference_deck,
                                                             &s_decks[reference_deck]);
    bool phase_target_available = beat_phase_align_target_ms(aligned_ms,
                                                            target_meta,
                                                            reference_position_ms,
                                                            reference_meta,
                                                            &aligned_ms);
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
        if (xQueueReceive(s_queue, &ev, portMAX_DELAY) != pdTRUE) continue;

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
            if (control_link_id_control(ev.id) == CTRL_DECK_CTL_JOG_SEARCH) {
                on_jog_search(deck, ev.value);
            } else {
                on_jog(deck, ev.value);
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
    for (uint8_t i = 0; i < DECK_CORE_DECK_COUNT; i++) {
        init_deck_state(&s_decks[i]);
    }
    init_beat_fx_state();
    flx4_led_publisher_init(&s_flx4_led_publisher);

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;

    s_queue = xQueueCreate(CTRL_QUEUE_LEN, sizeof(ctrl_event_t));
    if (!s_queue) return ESP_ERR_NO_MEM;

    if (xTaskCreate(deck_task, "deck", DECK_TASK_STACK_BYTES, NULL, 5, NULL) != pdPASS) {
        vQueueDelete(s_queue);
        s_queue = NULL;
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
        return ESP_ERR_NO_MEM;
    }

#if !defined(DECK_CORE_PC_TEST)
    if (!s_vu_timer) {
        const esp_timer_create_args_t vu_timer_args = {
            .callback = vu_timer_cb,
            .name = "flx4_vu",
        };
        esp_err_t timer_rc = esp_timer_create(&vu_timer_args, &s_vu_timer);
        if (timer_rc != ESP_OK) {
            vQueueDelete(s_queue);
            s_queue = NULL;
            vSemaphoreDelete(s_mutex);
            s_mutex = NULL;
            return timer_rc;
        }
    }
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_vu_timer, 30000));
#endif

    *ctrl_event_queue_out = s_queue;
    ESP_LOGI(TAG, "deck core ready");
    return ESP_OK;
}

esp_err_t deck_core_queue_event(const ctrl_event_t *ev)
{
    if (!s_queue || !ev) return ESP_ERR_INVALID_ARG;
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

deck_state_t deck_core_get_deck_state(uint8_t deck)
{
    uint8_t idx = normalize_deck(deck);
    bool uses_audio = deck_uses_audio_engine(idx);
    bool audio_playing = false;
    uint32_t audio_position_ms = 0;
    if (uses_audio) {
        audio_playing = audio_engine_deck_is_playing(idx);
        audio_position_ms = audio_engine_deck_position_ms(idx);
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    deck_state_t *state = &s_decks[idx];
    if (uses_audio) {
        state->playing = audio_playing;
        state->position_ms = audio_position_ms;
    }
    TickType_t now = xTaskGetTickCount();
    if (s_last_heartbeat_tick != 0) {
        uint32_t age_ms = (uint32_t)((now - s_last_heartbeat_tick) * portTICK_PERIOD_MS);
        state->last_heartbeat_age_ms = age_ms;
        state->control_link_connected = age_ms <= 10000u;
    } else {
        state->last_heartbeat_age_ms = UINT32_MAX;
        state->control_link_connected = false;
    }

    deck_state_t snap = *state;
    xSemaphoreGive(s_mutex);
    return snap;
}

deck_core_beat_fx_state_t deck_core_get_beat_fx_state(void)
{
    return s_beat_fx;
}

void deck_core_reset(void)
{
    deck_core_reset_deck(DECK_CORE_COMPAT_DECK);
}

void deck_core_reset_deck(uint8_t deck)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    uint8_t idx = normalize_deck(deck);
    init_deck_state(&s_decks[idx]);
    if (s_sync_master_deck == idx) {
        s_sync_master_deck = CTRL_DECK_NONE;
    }
    memset(&s_loop_shadow[idx], 0, sizeof(s_loop_shadow[idx]));
    memset(&s_shifted_loop_roll[idx], 0, sizeof(s_shifted_loop_roll[idx]));
    memset(&s_pad_fx_led[idx], 0, sizeof(s_pad_fx_led[idx]));
    memset(&s_beat_loop_led[idx], 0, sizeof(s_beat_loop_led[idx]));
    memset(&s_censor_shadow[idx], 0, sizeof(s_censor_shadow[idx]));
    xSemaphoreGive(s_mutex);
    ESP_LOGI(TAG, "deck %u core reset", (unsigned)idx + 1);
}

#if defined(DECK_CORE_PC_TEST)
void deck_core_test_reset(void)
{
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
#if defined(DECK_CORE_PC_TEST)
    memset(s_deferred_mixer_last, 0, sizeof(s_deferred_mixer_last));
    memset(s_deferred_mixer_seen, 0, sizeof(s_deferred_mixer_seen));
#endif
    flx4_led_publisher_init(&s_flx4_led_publisher);
    s_last_heartbeat_tick = 0;
    s_flx4_connection_state_valid = false;
    s_flx4_connected = false;
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
        if (control_link_id_control(ev->id) == CTRL_DECK_CTL_JOG_SEARCH) {
            on_jog_search(deck, ev->value);
        } else {
            on_jog(deck, ev->value);
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
