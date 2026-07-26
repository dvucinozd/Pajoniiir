#include "control_link.h"
#include "sdkconfig.h"
#include "s3_debug_ap.h"
#include "s3_ota.h"
#include "wifi_debug_log.h"
#include "firmware_health.h"
#include "flx4_midi_host.h"
#include "flx4_map.h"
#include "controller_profile_runtime.h"
#include "status_led.h"
#if CONFIG_P4_AUDIO_LINK_ENABLED
#include "p4_audio_link.h"
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
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
        }
#endif
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
#endif

#if CONFIG_DDJ_FLX4_TRANSLATE_TO_P4
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

static bool flx4_event_is_jog_touch(const flx4_control_event_t *ev)
{
    return ev && ev->type == CTRL_TYPE_BUTTON &&
           (ev->id == CTRL_ID_DECK1_JOG_TOUCH ||
            ev->id == CTRL_ID_DECK2_JOG_TOUCH);
}

static int16_t flx4_accumulate_delta(int16_t a, int16_t b)
{
    int32_t sum = (int32_t)a + (int32_t)b;
    if (sum > INT16_MAX) return INT16_MAX;
    if (sum < INT16_MIN) return INT16_MIN;
    return (int16_t)sum;
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
            if (flx4_event_is_relative_jog(ev)) {
                cur.value = flx4_accumulate_delta(cur.value, ev->value);
            } else {
                cur = *ev;  /* absolute control: newest value wins */
            }
            stash[stash_len++] = cur;
            replaced = true;
            continue;
        }
        stash[stash_len++] = cur;
    }

    for (int i = 0; i < stash_len; i++) {
        if (xQueueSend(s_flx4_event_queue, &stash[i], 0) != pdTRUE) {
            /* The FLX4 MIDI callback can refill the queue while it is drained
             * here; a failed re-push is a real lost event and must be counted
             * (matches the P4-side control_link coalescer). */
            s_flx4_dropped_count++;
        }
    }

    if (!replaced) {
        return false;
    }
    s_flx4_coalesced_count++;
    return true;
}

/* Touch edges are state transitions, not lossy high-rate motion. Keep only the
 * newest queued state for this platter, otherwise pushing a release to the
 * front ahead of an older press would invert the edges and leave scratch stuck.
 * Prefer sacrificing high-rate motion; if a queue contains buttons only, evict
 * one arbitrary oldest event as the final safety net. */
static bool flx4_enqueue_priority_touch(const flx4_control_event_t *ev)
{
    if (!flx4_event_is_jog_touch(ev) || !s_flx4_event_queue) return false;

    flx4_control_event_t stash[FLX4_EVENT_QUEUE_LEN];
    int stash_len = 0;
    flx4_control_event_t cur;
    while (stash_len < FLX4_EVENT_QUEUE_LEN &&
           xQueueReceive(s_flx4_event_queue, &cur, 0) == pdTRUE) {
        if (flx4_event_is_jog_touch(&cur) && cur.id == ev->id) {
            /* Latest level supersedes any undelivered edge for this platter. */
            s_flx4_coalesced_count++;
            continue;
        }
        stash[stash_len++] = cur;
    }

    if (stash_len == FLX4_EVENT_QUEUE_LEN) {
        for (int i = 0; i < stash_len; i++) {
            if (flx4_event_is_high_rate(&stash[i])) {
                for (int j = i + 1; j < stash_len; j++) {
                    stash[j - 1] = stash[j];
                }
                stash_len--;
                s_flx4_dropped_count++;
                break;
            }
        }
    }
    for (int i = 0; i < stash_len; i++) {
        if (xQueueSend(s_flx4_event_queue, &stash[i], 0) != pdTRUE) {
            s_flx4_dropped_count++;
        }
    }
    /* The translator may have drained a slot while the queue was rebuilt. */
    if (xQueueSendToFront(s_flx4_event_queue, ev, 0) == pdTRUE) return true;

    /* Button-only saturation: preserving the platter level is more important
     * than any single queued button edge. There is only one producer for this
     * queue, so the slot freed here cannot be stolen before SendToFront. */
    if (xQueueReceive(s_flx4_event_queue, &cur, 0) == pdTRUE) {
        s_flx4_dropped_count++;
        return xQueueSendToFront(s_flx4_event_queue, ev, portMAX_DELAY) == pdTRUE;
    }
    return false;
}

static void flx4_enqueue_event(const flx4_control_event_t *ev)
{
    if (xQueueSend(s_flx4_event_queue, ev, 0) == pdTRUE) {
        return;
    }
    if (!flx4_enqueue_priority_touch(ev) && !flx4_try_coalesce_latest(ev)) {
        s_flx4_dropped_count++;
    }
    flx4_translator_warn_rate_limited();
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
    controller_profile_runtime_init();
    s_flx4_event_queue = xQueueCreate(FLX4_EVENT_QUEUE_LEN, sizeof(flx4_control_event_t));
    ESP_ERROR_CHECK(s_flx4_event_queue ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(control_link_init());
    control_link_set_profile_activate_cb(flx4_profile_activate_cb);
    ESP_ERROR_CHECK(s3_debug_ap_init());
    ESP_ERROR_CHECK(s3_debug_ap_set_status_callback(s3_debug_ap_status_cb));
    flx4_midi_host_set_message_callback(flx4_midi_message_cb, NULL);
    ESP_ERROR_CHECK(flx4_midi_host_init());
    ESP_ERROR_CHECK(xTaskCreate(flx4_translator_task, "flx4_tx", 3072, NULL, 6, NULL) == pdPASS
                    ? ESP_OK : ESP_ERR_NO_MEM);
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
