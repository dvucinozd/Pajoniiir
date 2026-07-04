#include "wifi_link.h"
#include "web_server.h"

#include "esp_check.h"
#include "esp_event.h"
#include "esp_hosted.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "wifi_link";
static wifi_link_status_t s_status;
static bool s_netif_ready;          // esp_netif + event loop + handler (one-time)
static bool s_hosted_ready;         // esp_hosted transport initialised (per active cycle)
static bool s_wifi_ready;           // esp_wifi initialised (per active cycle)
static esp_netif_t *s_ap_netif;     // recreated each start, destroyed each stop

// Async enable/disable machinery. s_active is written only by the worker task;
// s_desired holds the latest requested state. A single worker collapses rapid
// toggles by looping until s_active == s_desired.
static SemaphoreHandle_t s_ctrl_lock;
static volatile bool s_active;
static volatile bool s_desired;
static volatile bool s_worker_running;

static void copy_wifi_bytes(uint8_t *dst, size_t dst_len, const char *src)
{
    if (!dst || dst_len == 0) {
        return;
    }
    memset(dst, 0, dst_len);
    if (!src) {
        return;
    }
    size_t n = strlen(src);
    if (n > dst_len) {
        n = dst_len;
    }
    memcpy(dst, src, n);
}

static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        s_status.ap_clients++;
        ESP_LOGI(TAG, "web client connected (%u)", (unsigned)s_status.ap_clients);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        if (s_status.ap_clients > 0) {
            s_status.ap_clients--;
        }
        ESP_LOGI(TAG, "web client disconnected (%u)", (unsigned)s_status.ap_clients);
    }
}

static esp_err_t ensure_wifi_stack(void)
{
    if (!s_netif_ready) {
        ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "esp_netif_init");
        esp_err_t rc = esp_event_loop_create_default();
        if (rc != ESP_OK && rc != ESP_ERR_INVALID_STATE) {
            return rc;
        }
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_handler_instance_register(
            WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL, NULL));
        s_netif_ready = true;
    }

    if (!s_hosted_ready) {
        ESP_RETURN_ON_ERROR(esp_hosted_init(), TAG, "esp_hosted_init");
        s_hosted_ready = true;
    }
    if (!s_wifi_ready) {
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "esp_wifi_init");
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_ps(WIFI_PS_NONE));
        s_wifi_ready = true;
    }
    return ESP_OK;
}

static esp_err_t start_web_ap(void)
{
    if (!s_ap_netif) {
        s_ap_netif = esp_netif_create_default_wifi_ap();
    }
    if (s_ap_netif) {
        esp_netif_ip_info_t ip_info = {0};
        ip_info.ip.addr = ESP_IP4TOADDR(192, 168, 4, 1);
        ip_info.gw.addr = ESP_IP4TOADDR(192, 168, 4, 1);
        ip_info.netmask.addr = ESP_IP4TOADDR(255, 255, 255, 0);
        esp_netif_dhcps_stop(s_ap_netif);
        esp_netif_set_ip_info(s_ap_netif, &ip_info);
        esp_netif_dhcps_start(s_ap_netif);
    }

    wifi_config_t cfg = {0};
    copy_wifi_bytes(cfg.ap.ssid, sizeof(cfg.ap.ssid), s_status.ssid);
    cfg.ap.ssid_len = (uint8_t)strlen(s_status.ssid);
    copy_wifi_bytes(cfg.ap.password, sizeof(cfg.ap.password), WIFI_LINK_PASSWORD);
    cfg.ap.channel = 6;
    cfg.ap.max_connection = 1;
    cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    cfg.ap.pmf_cfg.required = false;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG, "set AP mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &cfg), TAG, "set AP config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start AP");
    ESP_LOGI(TAG, "web AP started: ssid=%s", s_status.ssid);
    return ESP_OK;
}

esp_err_t wifi_link_init(void)
{
    memset(&s_status, 0, sizeof(s_status));
    snprintf(s_status.ssid, sizeof(s_status.ssid), "%s", WIFI_LINK_SOFTAP_SSID);
    if (!s_ctrl_lock) {
        s_ctrl_lock = xSemaphoreCreateMutex();
        if (!s_ctrl_lock) {
            ESP_LOGE(TAG, "failed to create control mutex");
            s_status.last_error = ESP_ERR_NO_MEM;
            return ESP_ERR_NO_MEM;
        }
    }
    ESP_LOGI(TAG, "wifi_link ready (remote off; call wifi_link_start to enable)");
    return ESP_OK;
}

esp_err_t wifi_link_start(void)
{
    if (s_active) {
        return ESP_OK;
    }

    esp_err_t rc = ensure_wifi_stack();
    if (rc == ESP_OK) {
        rc = start_web_ap();
    }
    if (rc == ESP_OK) {
        rc = web_server_start();
    }
    if (rc == ESP_OK) {
        rc = dns_server_start();
    }

    s_status.last_error = rc;
    s_status.initialized = (rc == ESP_OK);
    if (rc == ESP_OK) {
        s_active = true;
        s_status.active = true;
        ESP_LOGI(TAG, "Wi-Fi remote enabled");
    } else {
        ESP_LOGE(TAG, "Wi-Fi remote start failed: %s — tearing down", esp_err_to_name(rc));
        wifi_link_stop();  // roll back any partial bring-up
        s_status.last_error = rc;
    }
    return rc;
}

esp_err_t wifi_link_stop(void)
{
    // Tear down in reverse order; each step is best-effort so a partial
    // bring-up (from a failed start) still fully unwinds.
    dns_server_stop();
    web_server_stop();

    if (s_wifi_ready) {
        esp_wifi_stop();
        esp_wifi_deinit();
        s_wifi_ready = false;
    }
    if (s_ap_netif) {
        esp_netif_destroy_default_wifi(s_ap_netif);
        s_ap_netif = NULL;
    }
    // Release the ESP-Hosted transport / C6 link so it stops consuming RAM and
    // radio while the remote is off.
    if (s_hosted_ready) {
        esp_hosted_deinit();
        s_hosted_ready = false;
    }

    s_active = false;
    s_status.active = false;
    s_status.ap_clients = 0;
    ESP_LOGI(TAG, "Wi-Fi remote disabled");
    return ESP_OK;
}

static void wifi_link_worker(void *arg)
{
    (void)arg;
    for (;;) {
        xSemaphoreTake(s_ctrl_lock, portMAX_DELAY);
        bool desired = s_desired;
        bool active = s_active;
        if (desired == active) {
            s_worker_running = false;
            xSemaphoreGive(s_ctrl_lock);
            break;
        }
        xSemaphoreGive(s_ctrl_lock);

        if (desired) {
            wifi_link_start();
        } else {
            wifi_link_stop();
        }
    }
    vTaskDelete(NULL);
}

void wifi_link_request_enable(bool enable)
{
    if (!s_ctrl_lock) {
        // init not run — fall back to a direct (blocking) call.
        if (enable) {
            wifi_link_start();
        } else {
            wifi_link_stop();
        }
        return;
    }

    xSemaphoreTake(s_ctrl_lock, portMAX_DELAY);
    s_desired = enable;
    bool spawn = !s_worker_running;
    if (spawn) {
        s_worker_running = true;
    }
    xSemaphoreGive(s_ctrl_lock);

    if (spawn) {
        if (xTaskCreate(wifi_link_worker, "wifi_link", 6144, NULL, 4, NULL) != pdPASS) {
            ESP_LOGE(TAG, "failed to spawn wifi_link worker");
            xSemaphoreTake(s_ctrl_lock, portMAX_DELAY);
            s_worker_running = false;
            xSemaphoreGive(s_ctrl_lock);
        }
    }
}

bool wifi_link_is_active(void)
{
    return s_active;
}

wifi_link_status_t wifi_link_get_status(void)
{
    return s_status;
}
