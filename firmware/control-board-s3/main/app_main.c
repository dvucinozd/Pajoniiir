#include "panel_io.h"
#include "midi_compat.h"
#include "control_link.h"
#include "calibration.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "main";

// ─── Router task ──────────────────────────────────────────────────────────────
// Reads panel events and fans them out to both the MIDI compat layer and the
// UART control link to the ESP32-P4 main deck board.

static QueueHandle_t s_panel_queue;

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

// ─── Heartbeat task ───────────────────────────────────────────────────────────
// Sends CTRL_TYPE_HEARTBEAT to the P4 every 5 s.
// The P4 can use this to detect S3 disconnects (e.g. timeout > 10 s → offline).

static void heartbeat_task(void *arg)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        control_link_send_heartbeat();
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "CDJ100S-XXX control board firmware starting");
    ESP_LOGI(TAG, "Board: ESP32-S3-DevKitC-1 N16R8");

    ESP_ERROR_CHECK(calibration_init());
    ESP_ERROR_CHECK(panel_io_init(&s_panel_queue));
    ESP_ERROR_CHECK(midi_compat_init(s_panel_queue));
    ESP_ERROR_CHECK(control_link_init(s_panel_queue));

    ESP_ERROR_CHECK(xTaskCreate(router_task, "router", 3072, NULL, 6, NULL) == pdPASS
                    ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreate(heartbeat_task, "heartbeat", 1024, NULL, 3, NULL) == pdPASS
                    ? ESP_OK : ESP_ERR_NO_MEM);

    ESP_LOGI(TAG, "all subsystems ready");
}
