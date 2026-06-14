#include "panel_io.h"
#include "control_link.h"
#include "calibration.h"
#include "sdkconfig.h"
#if CONFIG_DDJ_FLX4_HOST_MODE
#include "flx4_midi_host.h"
#else
#include "midi_compat.h"
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "main";

// ─── Router task ──────────────────────────────────────────────────────────────
// Reads panel events and fans them out to both the MIDI compat layer and the
// UART control link to the ESP32-P4 main deck board.

#if !CONFIG_DDJ_FLX4_HOST_MODE
static QueueHandle_t s_panel_queue;
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
    ESP_ERROR_CHECK(control_link_init(NULL));
    ESP_ERROR_CHECK(flx4_midi_host_init());
    ESP_ERROR_CHECK(xTaskCreate(heartbeat_task, "heartbeat", 1024, NULL, 3, NULL) == pdPASS
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
    ESP_ERROR_CHECK(xTaskCreate(heartbeat_task, "heartbeat", 1024, NULL, 3, NULL) == pdPASS
                    ? ESP_OK : ESP_ERR_NO_MEM);
#endif

    ESP_LOGI(TAG, "all subsystems ready");
}
