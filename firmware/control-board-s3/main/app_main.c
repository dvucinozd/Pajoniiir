#include "control_link.h"
#include "sdkconfig.h"
#include "s3_debug_ap.h"
#include "s3_ota.h"
#include "wifi_debug_log.h"
#include "firmware_health.h"
#include "flx4_midi_host.h"
#include "flx4_map.h"
#include "controller_profile_runtime.h"
#include "control_state_reconciler.h"
#include "control_event_scheduler.h"
#include "status_led.h"
#if CONFIG_P4_AUDIO_LINK_ENABLED
#include "p4_audio_link.h"
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "main";

#define HEARTBEAT_TASK_STACK 3072

// ─── Router task ──────────────────────────────────────────────────────────────
// Reads panel events and fans them out to both the MIDI compat layer and the
// UART control link to the ESP32-P4 main deck board.

#if CONFIG_DDJ_FLX4_TRANSLATE_TO_P4
static void flx4_replay_known_input_snapshot(void);
static void flx4_replay_known_held_snapshot(void);
#endif

static void s3_debug_ap_status_cb(s3_debug_ap_status_t status)
{
    (void)control_link_send_semantic(CTRL_TYPE_STATE,
                                     CTRL_ID_S3_DEBUG_AP,
                                     (int16_t)status);
}

// ─── Heartbeat task ───────────────────────────────────────────────────────────
// Sends CTRL_TYPE_HEARTBEAT to the P4 every 5 s.
// The P4 can use this to detect S3 disconnects (e.g. timeout > 10 s → offline).

#if CONFIG_DDJ_FLX4_TRANSLATE_TO_P4
static void send_firmware_report(void)
{
    firmware_health_info_t info;
    if (firmware_health_get_info(&info) != ESP_OK) return;

    ctrl_firmware_report_t report = {0};
    if (strcmp(info.partition_label, "ota_0") == 0) {
        report.slot = CTRL_FW_SLOT_OTA_0;
    } else if (strcmp(info.partition_label, "ota_1") == 0) {
        report.slot = CTRL_FW_SLOT_OTA_1;
    } else if (strcmp(info.partition_label, "factory") == 0) {
        report.slot = CTRL_FW_SLOT_FACTORY;
    }
    switch (info.image_state) {
    case ESP_OTA_IMG_NEW: report.state = CTRL_FW_STATE_NEW; break;
    case ESP_OTA_IMG_PENDING_VERIFY: report.state = CTRL_FW_STATE_PENDING_VERIFY; break;
    case ESP_OTA_IMG_VALID: report.state = CTRL_FW_STATE_VALID; break;
    case ESP_OTA_IMG_INVALID: report.state = CTRL_FW_STATE_INVALID; break;
    case ESP_OTA_IMG_ABORTED: report.state = CTRL_FW_STATE_ABORTED; break;
    case ESP_OTA_IMG_UNDEFINED:
    default: report.state = CTRL_FW_STATE_UNKNOWN; break;
    }
    snprintf(report.version, sizeof(report.version), "%s", info.version);
    (void)control_link_send_firmware_report(&report);
}

static void heartbeat_task(void *arg)
{
    while (1) {
        control_link_send_heartbeat();
        send_firmware_report();
#if CONFIG_DDJ_FLX4_TRANSLATE_TO_P4
        if (flx4_midi_host_refresh_connection_state()) {
            flx4_replay_known_input_snapshot();
            flx4_replay_known_held_snapshot();
        }
#endif
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
#endif

#if CONFIG_DDJ_FLX4_TRANSLATE_TO_P4
#define FLX4_DISCRETE_BUDGET   8u
#define FLX4_CONTINUOUS_BUDGET 4u

static control_event_scheduler_t s_flx4_scheduler;
static portMUX_TYPE s_flx4_scheduler_mux = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_flx4_translator_task;
static flx4_map_state_t s_flx4_map;
static control_held_state_reconciler_t s_flx4_held_states;
static portMUX_TYPE s_flx4_held_mux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_flx4_unsupported_count;
static TickType_t s_flx4_last_warn;
static control_event_scheduler_stats_t s_flx4_last_warn_stats;

static void flx4_wake_translator(void);

typedef struct {
    size_t sent;
    size_t failed;
} flx4_snapshot_replay_ctx_t;

static bool flx4_send_snapshot_event(uint8_t type, uint8_t id, int16_t value, void *ctx)
{
    flx4_snapshot_replay_ctx_t *replay = (flx4_snapshot_replay_ctx_t *)ctx;
    esp_err_t rc = control_link_send_semantic(type, id, value);
    if (rc == ESP_OK) {
        replay->sent++;
    } else {
        replay->failed++;
    }
    return true;
}

static void flx4_replay_known_input_snapshot(void)
{
    flx4_snapshot_replay_ctx_t replay = { 0 };
    /* A dynamic profile, once activated, owns the input map (and its own
     * replay set); otherwise fall back to the built-in FLX4 map. */
    size_t known = controller_profile_runtime_active()
        ? controller_profile_runtime_emit_snapshot(flx4_send_snapshot_event, &replay)
        : flx4_map_emit_snapshot(&s_flx4_map, flx4_send_snapshot_event, &replay);
    if (known > 0 || replay.failed > 0) {
        ESP_LOGD(TAG, "FLX4 input snapshot replay known=%u sent=%u failed=%u",
                 (unsigned)known, (unsigned)replay.sent, (unsigned)replay.failed);
    }
}

static void flx4_flush_pending_held_states(void)
{
    size_t cursor = 0u;
    while (cursor < CONTROL_HELD_STATE_COUNT) {
        int key = -1;
        uint8_t id = 0u;
        uint8_t sequence = 0u;
        int16_t value = 0;
        portENTER_CRITICAL(&s_flx4_held_mux);
        bool found = control_held_state_next_dirty(
            &s_flx4_held_states, &cursor, &key, &id, &value, &sequence);
        portEXIT_CRITICAL(&s_flx4_held_mux);
        (void)sequence;
        if (!found) {
            break;
        }
        if (control_link_send_semantic(CTRL_TYPE_BUTTON, id, value) != ESP_OK) {
            break;
        }
        portENTER_CRITICAL(&s_flx4_held_mux);
        control_held_state_mark_scheduled(&s_flx4_held_states, key, value);
        portEXIT_CRITICAL(&s_flx4_held_mux);
    }
}

static void flx4_replay_known_held_snapshot(void)
{
    size_t cursor = 0u;
    while (cursor < CONTROL_HELD_STATE_COUNT) {
        uint8_t id = 0u;
        int16_t value = 0;
        portENTER_CRITICAL(&s_flx4_held_mux);
        bool found = control_held_state_next_observed(
            &s_flx4_held_states, &cursor, &id, &value);
        portEXIT_CRITICAL(&s_flx4_held_mux);
        if (!found) {
            break;
        }
        esp_err_t rc = control_link_send_semantic(CTRL_TYPE_BUTTON, id, value);
        if (rc != ESP_OK) {
            break;
        }
        const int key = control_held_state_key(id, value);
        portENTER_CRITICAL(&s_flx4_held_mux);
        control_held_state_mark_scheduled(&s_flx4_held_states, key, value);
        portEXIT_CRITICAL(&s_flx4_held_mux);
    }
}

static void flx4_controller_connection_cb(bool connected, void *user_ctx)
{
    (void)user_ctx;
    if (connected) {
        flx4_replay_known_input_snapshot();
        flx4_replay_known_held_snapshot();
        return;
    }
    portENTER_CRITICAL(&s_flx4_held_mux);
    control_held_state_release_all(&s_flx4_held_states, 0u);
    portEXIT_CRITICAL(&s_flx4_held_mux);
    flx4_wake_translator();
}

static bool flx4_event_is_high_rate(const flx4_control_event_t *ev)
{
    if (!ev) {
        return false;
    }
    if (ev->type == CTRL_TYPE_ENCODER &&
        ev->id != CTRL_ID_BROWSE_DELTA) {
        return true;
    }
    if (ev->type == CTRL_TYPE_PITCH) {
        switch (ev->id) {
        case CTRL_ID_DECK1_TEMPO:
        case CTRL_ID_DECK2_TEMPO:
        case CTRL_ID_CH1_VOLUME:
        case CTRL_ID_CH2_VOLUME:
        case CTRL_ID_CROSSFADER:
        case CTRL_ID_CH1_TRIM:
        case CTRL_ID_CH2_TRIM:
        case CTRL_ID_CH1_EQ_HIGH:
        case CTRL_ID_CH2_EQ_HIGH:
        case CTRL_ID_CH1_EQ_MID:
        case CTRL_ID_CH2_EQ_MID:
        case CTRL_ID_CH1_EQ_LOW:
        case CTRL_ID_CH2_EQ_LOW:
        case CTRL_ID_CH1_FILTER:
        case CTRL_ID_CH2_FILTER:
        case CTRL_ID_MASTER_VOLUME:
        case CTRL_ID_HEADPHONE_MIX:
        case CTRL_ID_HEADPHONE_LEVEL:
        case CTRL_ID_BEAT_FX_DEPTH:
            return true;
        default:
            return false;
        }
    }
    return false;
}

static bool flx4_event_is_relative_jog(const flx4_control_event_t *ev)
{
    if (!ev || ev->type != CTRL_TYPE_ENCODER) {
        return false;
    }
    switch (ev->id) {
    case CTRL_ID_DECK1_JOG_SCRATCH:
    case CTRL_ID_DECK1_JOG_BEND:
    case CTRL_ID_DECK1_JOG_SEARCH:
    case CTRL_ID_DECK2_JOG_SCRATCH:
    case CTRL_ID_DECK2_JOG_BEND:
    case CTRL_ID_DECK2_JOG_SEARCH:
        return true;
    default:
        return false;
    }
}

static void flx4_translator_warn_rate_limited(void)
{
    control_event_scheduler_stats_t stats;
    portENTER_CRITICAL(&s_flx4_scheduler_mux);
    stats = control_event_scheduler_get_stats(&s_flx4_scheduler);
    portEXIT_CRITICAL(&s_flx4_scheduler_mux);
    const uint32_t unsupported =
        __atomic_load_n(&s_flx4_unsupported_count, __ATOMIC_RELAXED);
    /* Coalescing and high-water movement are expected under normal control
     * bursts. Emit a warning only when capacity or numeric limits are hit. */
    if (stats.fifo_full == s_flx4_last_warn_stats.fifo_full &&
        stats.continuous_slot_full == s_flx4_last_warn_stats.continuous_slot_full &&
        stats.jog_saturated == s_flx4_last_warn_stats.jog_saturated) {
        return;
    }
    TickType_t now = xTaskGetTickCount();
    if (now - s_flx4_last_warn < pdMS_TO_TICKS(1000)) {
        return;
    }
    s_flx4_last_warn = now;
    s_flx4_last_warn_stats = stats;
    ESP_LOGW(TAG, "FLX4 scheduler fifo_full=%" PRIu32 " coalesced=%" PRIu32
             " jog_saturated=%" PRIu32 " slot_full=%" PRIu32
             " max_depth=%" PRIu32 " unsupported=%" PRIu32,
             stats.fifo_full, stats.continuous_coalesced, stats.jog_saturated,
             stats.continuous_slot_full, stats.max_fifo_depth,
             unsupported);
}

static void flx4_wake_translator(void)
{
    if (s_flx4_translator_task) {
        xTaskNotifyGive(s_flx4_translator_task);
    }
}

static void flx4_enqueue_event(const flx4_control_event_t *ev)
{
    if (!ev) {
        return;
    }
    if (ev && ev->type == CTRL_TYPE_BUTTON &&
        control_held_state_key(ev->id, ev->value) >= 0) {
        portENTER_CRITICAL(&s_flx4_held_mux);
        (void)control_held_state_observe(&s_flx4_held_states, ev->id, ev->value, 0u);
        portEXIT_CRITICAL(&s_flx4_held_mux);
        /* Held states are durable desired values, never lossy FIFO tokens. */
        flx4_wake_translator();
        return;
    }

    const control_scheduled_event_t scheduled = {
        .type = ev->type,
        .id = ev->id,
        .value = ev->value,
    };
    portENTER_CRITICAL(&s_flx4_scheduler_mux);
    if (flx4_event_is_high_rate(ev)) {
        (void)control_event_scheduler_publish_continuous(
            &s_flx4_scheduler, &scheduled,
            flx4_event_is_relative_jog(ev)
                ? CONTROL_EVENT_ACCUMULATE_DELTA
                : CONTROL_EVENT_LATEST_VALUE);
    } else {
        (void)control_event_scheduler_enqueue_discrete(&s_flx4_scheduler, &scheduled);
    }
    portEXIT_CRITICAL(&s_flx4_scheduler_mux);
    flx4_wake_translator();
}

static bool flx4_profile_activate_cb(const uint8_t *blob, size_t len,
                                     uint16_t vid, uint16_t pid)
{
    return controller_profile_runtime_activate(blob, len, vid, pid);
}

static void flx4_midi_message_cb(const flx4_midi_message_t *msg, void *user_ctx)
{
    (void)user_ctx;
    if (!msg || msg->len < 3) {
        return;
    }
    flx4_control_event_t ev;
    bool mapped;
    /* Prefer the active dynamic profile; fall back to the built-in FLX4 map
     * when none has been transferred/activated. */
    if (controller_profile_runtime_active()) {
        mapped = controller_profile_runtime_map(msg->status, msg->data1, msg->data2,
                                                &ev.type, &ev.id, &ev.value);
    } else {
        mapped = flx4_map_message(&s_flx4_map, msg, &ev);
    }
    if (mapped) {
        flx4_enqueue_event(&ev);
    } else {
        __atomic_fetch_add(&s_flx4_unsupported_count, 1u, __ATOMIC_RELAXED);
    }
}

static void flx4_schedule_pending_events(void)
{
    flx4_flush_pending_held_states();

    for (size_t i = 0; i < FLX4_DISCRETE_BUDGET; ++i) {
        control_scheduled_event_t event;
        portENTER_CRITICAL(&s_flx4_scheduler_mux);
        const bool found = control_event_scheduler_dequeue_discrete(
            &s_flx4_scheduler, &event);
        portEXIT_CRITICAL(&s_flx4_scheduler_mux);
        if (!found) {
            break;
        }
        (void)control_link_send_semantic(event.type, event.id, event.value);
    }

    for (size_t i = 0; i < FLX4_CONTINUOUS_BUDGET; ++i) {
        control_scheduled_event_t event;
        portENTER_CRITICAL(&s_flx4_scheduler_mux);
        const bool found = control_event_scheduler_take_continuous(
            &s_flx4_scheduler, &event);
        portEXIT_CRITICAL(&s_flx4_scheduler_mux);
        if (!found) {
            break;
        }
        (void)control_link_send_semantic(event.type, event.id, event.value);
    }
}

static void flx4_translator_task(void *arg)
{
    (void)arg;
    while (1) {
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(20));
        flx4_schedule_pending_events();
        flx4_translator_warn_rate_limited();
    }
}
#endif

void app_main(void)
{
    ESP_ERROR_CHECK(firmware_health_init());
    ESP_ERROR_CHECK(s3_ota_init());
    ESP_LOGI(TAG, "Pajoniiir control board firmware starting");
    ESP_LOGI(TAG, "Board: ESP32-S3-DevKitC-1 N16R8");
    (void)wifi_debug_log_init();

#if CONFIG_DDJ_FLX4_TRANSLATE_TO_P4
    ESP_LOGI(TAG, "mode: DDJ-FLX4 USB MIDI translator");
    if (status_led_init() != ESP_OK) {
        ESP_LOGW(TAG, "status LED unavailable; continuing without it");
    }
    flx4_map_init(&s_flx4_map);
    control_held_state_reset(&s_flx4_held_states);
    control_event_scheduler_reset(&s_flx4_scheduler);
    controller_profile_runtime_init();
    ESP_ERROR_CHECK(control_link_init());
    control_link_set_profile_activate_cb(flx4_profile_activate_cb);
    ESP_ERROR_CHECK(s3_debug_ap_init());
    ESP_ERROR_CHECK(s3_debug_ap_set_status_callback(s3_debug_ap_status_cb));
    ESP_ERROR_CHECK(xTaskCreate(flx4_translator_task, "flx4_tx", 3072, NULL, 6,
                                &s_flx4_translator_task) == pdPASS
                    ? ESP_OK : ESP_ERR_NO_MEM);
    flx4_midi_host_set_message_callback(flx4_midi_message_cb, NULL);
    flx4_midi_host_set_connection_callback(flx4_controller_connection_cb, NULL);
    ESP_ERROR_CHECK(flx4_midi_host_init());
    ESP_ERROR_CHECK(xTaskCreate(heartbeat_task, "heartbeat", HEARTBEAT_TASK_STACK, NULL, 3, NULL) == pdPASS
                    ? ESP_OK : ESP_ERR_NO_MEM);
#else
    ESP_LOGI(TAG, "mode: DDJ-FLX4 USB MIDI host raw logger");
    if (status_led_init() != ESP_OK) {
        ESP_LOGW(TAG, "status LED unavailable; continuing without it");
    }
    ESP_ERROR_CHECK(flx4_midi_host_init());
#endif

#if CONFIG_P4_AUDIO_LINK_ENABLED
    ESP_ERROR_CHECK(p4_audio_link_init());
    ESP_ERROR_CHECK(p4_audio_link_start());
#endif

    ESP_LOGI(TAG, "all subsystems ready");
    ESP_ERROR_CHECK(firmware_health_mark_ready());
}
