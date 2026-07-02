#include "panel_io.h"
#include "control_link.h"
#include "calibration.h"
#include "sdkconfig.h"
#if CONFIG_DDJ_FLX4_HOST_MODE
#include "flx4_midi_host.h"
#include "flx4_map.h"
#else
#include "midi_compat.h"
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_err.h"
#include "esp_log.h"
#include <inttypes.h>

static const char *TAG = "main";

#define HEARTBEAT_TASK_STACK 3072

// ─── Router task ──────────────────────────────────────────────────────────────
// Reads panel events and fans them out to both the MIDI compat layer and the
// UART control link to the ESP32-P4 main deck board.

#if !CONFIG_DDJ_FLX4_HOST_MODE
static QueueHandle_t s_panel_queue;
#endif

#if CONFIG_DDJ_FLX4_HOST_MODE && CONFIG_DDJ_FLX4_TRANSLATE_TO_P4
static void flx4_replay_known_input_snapshot(void);
#endif

#if !CONFIG_DDJ_FLX4_HOST_MODE
static void router_task(void *arg)
{
    panel_event_t ev;
    while (1) {
        if (xQueueReceive(s_panel_queue, &ev, portMAX_DELAY) == pdTRUE) {
            midi_compat_process_event(&ev);
            control_link_send_event(&ev);
        }
    }
}
#endif

// ─── Heartbeat task ───────────────────────────────────────────────────────────
// Sends CTRL_TYPE_HEARTBEAT to the P4 every 5 s.
// The P4 can use this to detect S3 disconnects (e.g. timeout > 10 s → offline).

#if !CONFIG_DDJ_FLX4_HOST_MODE || CONFIG_DDJ_FLX4_TRANSLATE_TO_P4
static void heartbeat_task(void *arg)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        control_link_send_heartbeat();
#if CONFIG_DDJ_FLX4_HOST_MODE && CONFIG_DDJ_FLX4_TRANSLATE_TO_P4
        if (flx4_midi_host_refresh_connection_state()) {
            flx4_replay_known_input_snapshot();
        }
#endif
    }
}
#endif

#if CONFIG_DDJ_FLX4_HOST_MODE && CONFIG_DDJ_FLX4_TRANSLATE_TO_P4
#define FLX4_EVENT_QUEUE_LEN 32

static QueueHandle_t s_flx4_event_queue;
static flx4_map_state_t s_flx4_map;
static uint32_t s_flx4_dropped_count;
static uint32_t s_flx4_coalesced_count;
static uint32_t s_flx4_unsupported_count;
static TickType_t s_flx4_last_warn;

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
    size_t known = flx4_map_emit_snapshot(&s_flx4_map,
                                          flx4_send_snapshot_event,
                                          &replay);
    if (known > 0 || replay.failed > 0) {
        ESP_LOGD(TAG, "FLX4 input snapshot replay known=%u sent=%u failed=%u",
                 (unsigned)known, (unsigned)replay.sent, (unsigned)replay.failed);
    }
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
        return ev->id == CTRL_ID_DECK1_TEMPO ||
               ev->id == CTRL_ID_DECK2_TEMPO ||
               ev->id == CTRL_ID_CH1_VOLUME ||
               ev->id == CTRL_ID_CH2_VOLUME ||
               ev->id == CTRL_ID_CROSSFADER;
    }
    return false;
}

static void flx4_translator_warn_rate_limited(void)
{
    TickType_t now = xTaskGetTickCount();
    if (now - s_flx4_last_warn < pdMS_TO_TICKS(1000)) {
        return;
    }
    s_flx4_last_warn = now;
    ESP_LOGW(TAG, "FLX4 translator queue dropped=%" PRIu32 " coalesced=%" PRIu32
             " unsupported=%" PRIu32,
             s_flx4_dropped_count, s_flx4_coalesced_count, s_flx4_unsupported_count);
}

static bool flx4_try_coalesce_latest(const flx4_control_event_t *ev)
{
    if (!flx4_event_is_high_rate(ev) || !s_flx4_event_queue) {
        return false;
    }

    flx4_control_event_t stash[FLX4_EVENT_QUEUE_LEN];
    int stash_len = 0;
    bool replaced = false;
    flx4_control_event_t cur;

    while (stash_len < FLX4_EVENT_QUEUE_LEN &&
           xQueueReceive(s_flx4_event_queue, &cur, 0) == pdTRUE) {
        if (!replaced && cur.type == ev->type && cur.id == ev->id) {
            replaced = true;
            continue;
        }
        stash[stash_len++] = cur;
    }

    for (int i = 0; i < stash_len; i++) {
        (void)xQueueSend(s_flx4_event_queue, &stash[i], 0);
    }

    if (!replaced) {
        return false;
    }
    if (xQueueSend(s_flx4_event_queue, ev, 0) == pdTRUE) {
        s_flx4_coalesced_count++;
        return true;
    }
    return false;
}

static void flx4_enqueue_event(const flx4_control_event_t *ev)
{
    if (xQueueSend(s_flx4_event_queue, ev, 0) == pdTRUE) {
        return;
    }
    if (!flx4_try_coalesce_latest(ev)) {
        s_flx4_dropped_count++;
    }
    flx4_translator_warn_rate_limited();
}

static void flx4_midi_message_cb(const flx4_midi_message_t *msg, void *user_ctx)
{
    (void)user_ctx;
    flx4_control_event_t ev;
    if (flx4_map_message(&s_flx4_map, msg, &ev)) {
        flx4_enqueue_event(&ev);
    } else {
        s_flx4_unsupported_count++;
    }
}

static void flx4_translator_task(void *arg)
{
    (void)arg;
    flx4_control_event_t ev;
    while (1) {
        if (xQueueReceive(s_flx4_event_queue, &ev, portMAX_DELAY) == pdTRUE) {
            (void)control_link_send_semantic(ev.type, ev.id, ev.value);
        }
    }
}
#endif

void app_main(void)
{
    ESP_LOGI(TAG, "DDJ-FFL4 control board firmware starting");
    ESP_LOGI(TAG, "Board: ESP32-S3-DevKitC-1 N16R8");

#if CONFIG_DDJ_FLX4_HOST_MODE
#if CONFIG_DDJ_FLX4_TRANSLATE_TO_P4
    ESP_LOGI(TAG, "mode: DDJ-FLX4 USB MIDI translator");
    flx4_map_init(&s_flx4_map);
    s_flx4_event_queue = xQueueCreate(FLX4_EVENT_QUEUE_LEN, sizeof(flx4_control_event_t));
    ESP_ERROR_CHECK(s_flx4_event_queue ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(control_link_init(NULL));
    flx4_midi_host_set_message_callback(flx4_midi_message_cb, NULL);
    ESP_ERROR_CHECK(flx4_midi_host_init());
    ESP_ERROR_CHECK(xTaskCreate(flx4_translator_task, "flx4_tx", 3072, NULL, 6, NULL) == pdPASS
                    ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreate(heartbeat_task, "heartbeat", HEARTBEAT_TASK_STACK, NULL, 3, NULL) == pdPASS
                    ? ESP_OK : ESP_ERR_NO_MEM);
#else
    ESP_LOGI(TAG, "mode: DDJ-FLX4 USB MIDI host raw logger");
    ESP_ERROR_CHECK(flx4_midi_host_init());
#endif
#else
    ESP_LOGI(TAG, "mode: legacy CDJ panel + USB MIDI device compatibility");
    ESP_ERROR_CHECK(calibration_init());
    ESP_ERROR_CHECK(panel_io_init(&s_panel_queue));
    ESP_ERROR_CHECK(midi_compat_init(s_panel_queue));
    ESP_ERROR_CHECK(control_link_init(s_panel_queue));

    ESP_ERROR_CHECK(xTaskCreate(router_task, "router", 3072, NULL, 6, NULL) == pdPASS
                    ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreate(heartbeat_task, "heartbeat", HEARTBEAT_TASK_STACK, NULL, 3, NULL) == pdPASS
                    ? ESP_OK : ESP_ERR_NO_MEM);
#endif

    ESP_LOGI(TAG, "all subsystems ready");
}
