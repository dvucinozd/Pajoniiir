/*
 * Serialize temporary AP->STA->AP transitions with pull OTA.
 *
 * wifi_link.c remains the owner of Wi-Fi state. This wrapper only reserves the
 * cross-component transition lease before spawning the probe task and releases
 * it when that task has restored the AP and is about to exit.
 */
#include "wifi_transition_lease.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static void wifi_link_leased_task_delete(TaskHandle_t task);

#define wifi_link_probe_start wifi_link_probe_start_unleased
#define vTaskDelete wifi_link_leased_task_delete
#include "wifi_link.c"
#undef vTaskDelete
#undef wifi_link_probe_start

static void wifi_link_leased_task_delete(TaskHandle_t task)
{
    if (task == NULL &&
        !s_probe_running &&
        wifi_transition_lease_owner() == WIFI_TRANSITION_OWNER_PROBE) {
        wifi_transition_lease_release(WIFI_TRANSITION_OWNER_PROBE);
    }
    vTaskDelete(task);
}

esp_err_t wifi_link_probe_start(void)
{
    esp_err_t lease_rc =
        wifi_transition_lease_acquire(WIFI_TRANSITION_OWNER_PROBE);
    if (lease_rc != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi transition busy (owner=%d)",
                 (int)wifi_transition_lease_owner());
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t rc = wifi_link_probe_start_unleased();
    if (rc != ESP_OK) {
        wifi_transition_lease_release(WIFI_TRANSITION_OWNER_PROBE);
    }
    return rc;
}
