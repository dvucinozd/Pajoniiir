#include "wifi_link.h"

#include "esp_log.h"

#include <string.h>

static const char *TAG = "wifi_link";
static wifi_link_status_t s_status;

esp_err_t wifi_link_init(void)
{
    memset(&s_status, 0, sizeof(s_status));
    snprintf(s_status.ssid, sizeof(s_status.ssid), "%s", WIFI_LINK_SOFTAP_SSID);
    s_status.last_error = ESP_ERR_NOT_SUPPORTED;
    ESP_LOGI(TAG, "ESP-Hosted Wi-Fi disabled; web AP not started");
    return ESP_ERR_NOT_SUPPORTED;
}

wifi_link_status_t wifi_link_get_status(void)
{
    return s_status;
}
