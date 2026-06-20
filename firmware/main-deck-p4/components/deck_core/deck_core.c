#include "deck_core.h"
#include "control_link.h"
#include "flx4_led_snapshot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "audio_engine.h"
#include <inttypes.h>
#include <string.h>

static const char *TAG = "deck";

#define CTRL_QUEUE_LEN  32
#define PITCH_CENTER    8192
#define SEARCH_STEP_MS  5000

extern bool ui_is_library_active(void) __attribute__((weak));
extern esp_err_t ui_show_library(void) __attribute__((weak));
extern esp_err_t ui_toggle_library_view(void) __attribute__((weak));
extern esp_err_t ui_library_select_delta(int delta) __attribute__((weak));
extern esp_err_t ui_library_load_selected(void) __attribute__((weak));
extern esp_err_t ui_library_load_selected_for_deck(uint8_t deck) __attribute__((weak));

static QueueHandle_t    s_queue;
static SemaphoreHandle_t s_mutex;
static deck_state_t     s_decks[DECK_CORE_DECK_COUNT];
static flx4_led_publisher_t s_flx4_led_publisher;
static uint32_t          s_drop_count;
static TickType_t        s_last_drop_warn;
static TickType_t        s_last_heartbeat_tick;

static void init_deck_state(deck_state_t *state)
{
    memset(state, 0, sizeof(*state));
    state->pitch = PITCH_CENTER;
}

static uint8_t normalize_deck(uint8_t deck)
{
    return deck < DECK_CORE_DECK_COUNT ? deck : DECK_CORE_COMPAT_DECK;
}

static uint8_t deck_index_for_event(const ctrl_event_t *ev)
{
    if (ev && ev->id == CTRL_ID_LOAD_DECK1) {
        return CTRL_DECK_1;
    }
    if (ev && ev->id == CTRL_ID_LOAD_DECK2) {
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

static bool event_is_mixer_control(const ctrl_event_t *ev)
{
    return ev && (ev->id == CTRL_ID_CH1_VOLUME ||
                  ev->id == CTRL_ID_CH2_VOLUME ||
                  ev->id == CTRL_ID_CROSSFADER ||
                  ev->id == CTRL_ID_DECK1_PFL ||
                  ev->id == CTRL_ID_DECK2_PFL);
}

static button_id_t button_for_event(const ctrl_event_t *ev)
{
    if (ev && (ev->id == CTRL_ID_LOAD_DECK1 ||
               ev->id == CTRL_ID_LOAD_DECK2)) {
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

    for (uint8_t deck = 0; deck < DECK_CORE_DECK_COUNT; deck++) {
        deck_state_t state = deck_core_get_deck_state(deck);
        input.cue[deck] = state.position_ms == state.cue_point_ms ? 1 : 0;
        input.play[deck] = state.playing ? 1 : 0;
        input.pfl[deck] = audio_engine_get_pfl_enabled(deck) ? 1 : 0;
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
        ESP_LOGI(TAG, "FLX4 connected; forcing LED snapshot");
        publish_flx4_led_snapshot(true);
    } else if (ev->value == CTRL_FLX4_DISCONNECTED) {
        ESP_LOGI(TAG, "FLX4 disconnected");
    } else {
        ESP_LOGW(TAG, "unknown FLX4 connection state %d", ev->value);
    }
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

static void on_browse(int16_t delta)
{
    if (delta == 0) return;
    if (ui_library_select_delta) {
        esp_err_t rc = ui_library_select_delta(delta);
        ESP_LOGD(TAG, "browse %+d → %s", delta, esp_err_to_name(rc));
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

static void on_pitch(uint8_t deck, int16_t raw)
{
    deck_state_t *state = &s_decks[normalize_deck(deck)];
    state->pitch = raw;
    if (deck_uses_audio_engine(deck)) {
        audio_engine_deck_set_pitch(deck, raw);
    }
    ESP_LOGD(TAG, "deck %u pitch %d (center %d, offset %+d)",
             (unsigned)deck + 1, raw, PITCH_CENTER, raw - PITCH_CENTER);
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
           ev->id == CTRL_ID_BROWSE_PRESS;
}

// ─── Main task ────────────────────────────────────────────────────────────────

static void deck_task(void *arg)
{
    ctrl_event_t ev;
    while (1) {
        if (xQueueReceive(s_queue, &ev, portMAX_DELAY) != pdTRUE) continue;

        if (event_uses_ui_without_deck_state(&ev)) {
            if (ev.type == CTRL_EV_BROWSE) {
                on_browse(ev.value);
            } else if (ev.id == CTRL_ID_BROWSE_PRESS) {
                on_browse_press();
            } else {
                on_button(DECK_CORE_COMPAT_DECK, button_for_event(&ev), ev.value != 0);
            }
            continue;
        }

        if (ev.type == CTRL_EV_STATE) {
            on_state_event(&ev);
            continue;
        }

        uint8_t deck = deck_index_for_event(&ev);

        if (event_is_mixer_control(&ev)) {
            on_mixer_control(ev.id, ev.value);
            continue;
        }

        switch (ev.type) {
        case CTRL_EV_BUTTON:
            on_button(deck, button_for_event(&ev), ev.value != 0);
            break;
        case CTRL_EV_JOG:
            on_jog(deck, ev.value);
            break;
        case CTRL_EV_BROWSE:
            on_browse(ev.value);
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
    flx4_led_publisher_init(&s_flx4_led_publisher);

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;

    s_queue = xQueueCreate(CTRL_QUEUE_LEN, sizeof(ctrl_event_t));
    if (!s_queue) return ESP_ERR_NO_MEM;

    if (xTaskCreate(deck_task, "deck", 4096, NULL, 5, NULL) != pdPASS) {
        vQueueDelete(s_queue);
        s_queue = NULL;
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
        return ESP_ERR_NO_MEM;
    }

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

void deck_core_reset(void)
{
    deck_core_reset_deck(DECK_CORE_COMPAT_DECK);
}

void deck_core_reset_deck(uint8_t deck)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    uint8_t idx = normalize_deck(deck);
    init_deck_state(&s_decks[idx]);
    xSemaphoreGive(s_mutex);
    ESP_LOGI(TAG, "deck %u core reset", (unsigned)idx + 1);
}

#if defined(DECK_CORE_PC_TEST)
void deck_core_test_reset(void)
{
    for (uint8_t i = 0; i < DECK_CORE_DECK_COUNT; i++) {
        init_deck_state(&s_decks[i]);
    }
    flx4_led_publisher_init(&s_flx4_led_publisher);
    s_last_heartbeat_tick = 0;
}

void deck_core_test_apply_event(const ctrl_event_t *ev)
{
    if (!ev) return;

    if (event_uses_ui_without_deck_state(ev)) {
        if (ev->type == CTRL_EV_BROWSE) {
            on_browse(ev->value);
        } else if (ev->id == CTRL_ID_BROWSE_PRESS) {
            on_browse_press();
        } else {
            on_button(DECK_CORE_COMPAT_DECK, button_for_event(ev), ev->value != 0);
        }
        return;
    }

    if (ev->type == CTRL_EV_STATE) {
        on_state_event(ev);
        return;
    }

    uint8_t deck = deck_index_for_event(ev);

    if (event_is_mixer_control(ev)) {
        on_mixer_control(ev->id, ev->value);
        return;
    }

    switch (ev->type) {
    case CTRL_EV_BUTTON:
        on_button(deck, button_for_event(ev), ev->value != 0);
        break;
    case CTRL_EV_JOG:
        on_jog(deck, ev->value);
        break;
    case CTRL_EV_BROWSE:
        on_browse(ev->value);
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
#endif
