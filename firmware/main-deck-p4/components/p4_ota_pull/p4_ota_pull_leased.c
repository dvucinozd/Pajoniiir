/*
 * Serialize pull-OTA AP->STA->AP transitions with the Wi-Fi connectivity probe.
 *
 * Both check and install reserve the same lease before their worker task is
 * created. The worker releases it only after AP restoration and immediately
 * before task exit; a successful install intentionally holds it until restart.
 */
#include "wifi_transition_lease.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static void p4_ota_pull_leased_task_delete(TaskHandle_t task);

#define p4_ota_pull_check_start p4_ota_pull_check_start_unleased
#define p4_ota_pull_install_start p4_ota_pull_install_start_unleased
#define vTaskDelete p4_ota_pull_leased_task_delete
#include "p4_ota_pull.c"
#undef vTaskDelete
#undef p4_ota_pull_install_start
#undef p4_ota_pull_check_start

static void p4_ota_pull_leased_task_delete(TaskHandle_t task)
{
    if (task == NULL &&
        !s_running &&
        wifi_transition_lease_owner() == WIFI_TRANSITION_OWNER_OTA) {
        wifi_transition_lease_release(WIFI_TRANSITION_OWNER_OTA);
    }
    vTaskDelete(task);
}

static esp_err_t start_with_ota_lease(esp_err_t (*start_fn)(void))
{
    esp_err_t lease_rc =
        wifi_transition_lease_acquire(WIFI_TRANSITION_OWNER_OTA);
    if (lease_rc != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi transition busy (owner=%d)",
                 (int)wifi_transition_lease_owner());
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t rc = start_fn();
    if (rc != ESP_OK) {
        wifi_transition_lease_release(WIFI_TRANSITION_OWNER_OTA);
    }
    return rc;
}

esp_err_t p4_ota_pull_check_start(void)
{
    return start_with_ota_lease(p4_ota_pull_check_start_unleased);
}

esp_err_t p4_ota_pull_install_start(const char *expected_release)
{
    esp_err_t lease_rc =
        wifi_transition_lease_acquire(WIFI_TRANSITION_OWNER_OTA);
    if (lease_rc != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi transition busy (owner=%d)",
                 (int)wifi_transition_lease_owner());
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t rc = p4_ota_pull_install_start_unleased(expected_release);
    if (rc != ESP_OK) {
        wifi_transition_lease_release(WIFI_TRANSITION_OWNER_OTA);
    }
    return rc;
}
