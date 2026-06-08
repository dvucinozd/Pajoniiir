#include "deck_core.h"
#include "control_link.h"
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
extern esp_err_t ui_library_select_delta(int delta) __attribute__((weak));
extern esp_err_t ui_library_load_selected(void) __attribute__((weak));

static QueueHandle_t    s_queue;
static SemaphoreHandle_t s_mutex;
static deck_state_t     s_state;
static uint32_t          s_drop_count;
static TickType_t        s_last_drop_warn;
static TickType_t        s_last_heartbeat_tick;

// ─── LED sync ─────────────────────────────────────────────────────────────────

static void sync_leds(void)
{
    control_link_send_led(LED_PLAY, s_state.playing ? 1 : 0);
    control_link_send_led(LED_CUE,  (s_state.position_ms == s_state.cue_point_ms) ? 1 : 0);
}

// ─── Event handlers ───────────────────────────────────────────────────────────

static void on_button(button_id_t btn, bool pressed)
{
    if (!pressed) return;

    switch (btn) {
    case BTN_PLAY:
        if (audio_engine_is_playing()) {
            audio_engine_pause();
            s_state.playing = false;
        } else {
            audio_engine_play();
            s_state.playing = true;
        }
        ESP_LOGI(TAG, "play → %s", s_state.playing ? "PLAYING" : "PAUSED");
        sync_leds();
        break;

    case BTN_CUE:
        // Return to the cue point (track start by default) and pause — works
        // whether the deck is playing or already paused. Custom cue points are
        // handled by the hot-cue pads, so CUE here is a reliable "back to cue".
        audio_engine_pause();
        audio_engine_seek(s_state.cue_point_ms);
        s_state.playing     = false;
        s_state.position_ms = s_state.cue_point_ms;
        ESP_LOGI(TAG, "cue → %lu ms (paused)", (unsigned long)s_state.cue_point_ms);
        sync_leds();
        break;

    case BTN_MODE:
        s_state.perf_mode = (perf_mode_t)((s_state.perf_mode + 1) % PERF_MODE_COUNT);
        ESP_LOGI(TAG, "perf mode → %d", s_state.perf_mode);
        break;

    case BTN_MASTER_TEMPO:
        s_state.master_tempo = !s_state.master_tempo;
        ESP_LOGI(TAG, "master tempo → %s", s_state.master_tempo ? "ON" : "OFF");
        break;

    case BTN_EJECT:
        audio_engine_stop();
        s_state.playing      = false;
        s_state.position_ms  = 0;
        s_state.cue_point_ms = 0;
        ESP_LOGI(TAG, "eject");
        sync_leds();
        break;

    case BTN_LOAD:
        if (ui_library_load_selected) {
            esp_err_t rc = ui_library_load_selected();
            ESP_LOGI(TAG, "load selected → %s", esp_err_to_name(rc));
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
        uint32_t current = audio_engine_position_ms();
        int32_t target = (int32_t)current + (btn == BTN_SEARCH_FWD ? SEARCH_STEP_MS : -SEARCH_STEP_MS);
        if (target < 0) target = 0;
        esp_err_t rc = audio_engine_seek((uint32_t)target);
        if (rc == ESP_OK) {
            s_state.position_ms = (uint32_t)target;
            ESP_LOGI(TAG, "search %s → %lu ms",
                     btn == BTN_SEARCH_FWD ? "fwd" : "back",
                     (unsigned long)s_state.position_ms);
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
                 btn, s_state.perf_mode);
        break;

    default:
        ESP_LOGD(TAG, "btn %d pressed (unhandled in MVP)", btn);
        break;
    }
}

static void on_jog(int16_t delta)
{
    if (s_state.playing) {
        // Nudge: shift position slightly (placeholder — audio_engine handles real nudge)
        ESP_LOGD(TAG, "jog nudge %+d", delta);
    } else {
        // Scratch: advance/rewind position while paused
        int32_t pos = (int32_t)s_state.position_ms + delta * 3;
        s_state.position_ms = (pos < 0) ? 0 : (uint32_t)pos;
        audio_engine_seek(s_state.position_ms);
        ESP_LOGD(TAG, "jog scratch → %lu ms", s_state.position_ms);
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

static void on_pitch(int16_t raw)
{
    s_state.pitch = raw;
    audio_engine_set_pitch(raw);
    ESP_LOGD(TAG, "pitch %d (center %d, offset %+d)", raw, PITCH_CENTER, raw - PITCH_CENTER);
}

static bool event_uses_ui_without_deck_state(const ctrl_event_t *ev)
{
    if (ev->type == CTRL_EV_BROWSE) {
        return true;
    }
    if (ev->type != CTRL_EV_BUTTON || ev->value == 0) {
        return false;
    }
    return ev->id == BTN_LOAD || ev->id == BTN_TRACK_PREV || ev->id == BTN_TRACK_NEXT;
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
            } else {
                on_button((button_id_t)ev.id, ev.value != 0);
            }
            continue;
        }

        xSemaphoreTake(s_mutex, portMAX_DELAY);

        switch (ev.type) {
        case CTRL_EV_BUTTON:
            on_button((button_id_t)ev.id, ev.value != 0);
            break;
        case CTRL_EV_JOG:
            on_jog(ev.value);
            break;
        case CTRL_EV_BROWSE:
            on_browse(ev.value);
            break;
        case CTRL_EV_PITCH:
            on_pitch(ev.value);
            break;
        case CTRL_EV_HEARTBEAT:
            s_last_heartbeat_tick = xTaskGetTickCount();
            ESP_LOGD(TAG, "S3 heartbeat seq=%d", ev.seq);
            break;
        }

        xSemaphoreGive(s_mutex);
    }
}

// ─── Public API ───────────────────────────────────────────────────────────────

esp_err_t deck_core_init(QueueHandle_t *ctrl_event_queue_out)
{
    memset(&s_state, 0, sizeof(s_state));
    s_state.pitch = PITCH_CENTER;

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
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    
    // Sync status and playhead from audio_engine in real-time
    if (audio_engine_is_playing()) {
        s_state.playing     = true;
        s_state.position_ms = audio_engine_position_ms();
    } else {
        s_state.playing     = false;
        // Keep position_ms as is (where it paused/stopped)
    }
    TickType_t now = xTaskGetTickCount();
    if (s_last_heartbeat_tick != 0) {
        uint32_t age_ms = (uint32_t)((now - s_last_heartbeat_tick) * portTICK_PERIOD_MS);
        s_state.last_heartbeat_age_ms = age_ms;
        s_state.control_link_connected = age_ms <= 10000u;
    } else {
        s_state.last_heartbeat_age_ms = UINT32_MAX;
        s_state.control_link_connected = false;
    }

    deck_state_t snap = s_state;
    xSemaphoreGive(s_mutex);
    return snap;
}

void deck_core_reset(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_state.playing      = false;
    s_state.position_ms  = 0;
    s_state.cue_point_ms = 0;
    xSemaphoreGive(s_mutex);
    ESP_LOGI(TAG, "deck core reset");
}
