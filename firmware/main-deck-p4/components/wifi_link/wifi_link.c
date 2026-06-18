#include "wifi_link.h"

#include "esp_check.h"
#include "esp_event.h"
#include "esp_hosted.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include <string.h>

static const char *TAG = "wifi_link";
static wifi_link_status_t s_status;
static bool s_netif_ready;
static bool s_wifi_ready;

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

    if (!s_wifi_ready) {
        ESP_RETURN_ON_ERROR(esp_hosted_init(), TAG, "esp_hosted_init");
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "esp_wifi_init");
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_ps(WIFI_PS_NONE));
        s_wifi_ready = true;
    }
    return ESP_OK;
}

static esp_err_t start_web_ap(void)
{
    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    if (ap_netif) {
        esp_netif_ip_info_t ip_info = {0};
        ip_info.ip.addr = ESP_IP4TOADDR(192, 168, 4, 1);
        ip_info.gw.addr = ESP_IP4TOADDR(192, 168, 4, 1);
        ip_info.netmask.addr = ESP_IP4TOADDR(255, 255, 255, 0);
        esp_netif_dhcps_stop(ap_netif);
        esp_netif_set_ip_info(ap_netif, &ip_info);
        esp_netif_dhcps_start(ap_netif);
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

    ESP_RETURN_ON_ERROR(ensure_wifi_stack(), TAG, "wireless stack");
    esp_err_t rc = start_web_ap();
    s_status.initialized = (rc == ESP_OK);
    s_status.last_error = rc;
    return rc;
}

wifi_link_status_t wifi_link_get_status(void)
{
    return s_status;
}
