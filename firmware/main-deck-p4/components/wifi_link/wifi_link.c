#include "wifi_link.h"

#include "esp_check.h"
#include "esp_event.h"
#include "esp_hosted.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "wifi_link";
static wifi_link_status_t s_status = {
    .mode = WIFI_LINK_MODE_OFF,
};
static bool s_netif_ready;
static bool s_wifi_ready;
static TaskHandle_t s_join_task;

static esp_err_t make_identity(void)
{
    uint8_t mac[6] = {0};
    esp_err_t rc = esp_wifi_get_mac(WIFI_IF_STA, mac);
    if (rc != ESP_OK) {
        rc = esp_efuse_mac_get_default(mac);
    }
    if (rc != ESP_OK) {
        s_status.last_error = rc;
        return rc;
    }
    bool all_zero = true;
    for (size_t i = 0; i < sizeof(mac); i++) {
        if (mac[i] != 0) {
            all_zero = false;
            break;
        }
    }
    if (all_zero) {
        s_status.last_error = ESP_ERR_INVALID_STATE;
        return ESP_ERR_INVALID_STATE;
    }
    snprintf(s_status.peer_id, sizeof(s_status.peer_id), "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    snprintf(s_status.ssid, sizeof(s_status.ssid), "%s", WIFI_LINK_SOFTAP_PREFIX);
    return ESP_OK;
}

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
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        s_status.ap_clients++;
        ESP_LOGI(TAG, "join client connected (%u)", (unsigned)s_status.ap_clients);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        if (s_status.ap_clients > 0) {
            s_status.ap_clients--;
        }
        ESP_LOGI(TAG, "join client disconnected (%u)", (unsigned)s_status.ap_clients);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_status.connected = false;
        ESP_LOGW(TAG, "host AP disconnected; retrying");
        if (s_status.ssid[0]) {
            esp_wifi_connect();
        } else {
            s_status.scanning = true;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        s_status.connected = true;
        s_status.scanning = false;
        s_status.last_error = ESP_OK;
        ESP_LOGI(TAG, "joined host AP");
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
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                                          event_handler, NULL, NULL));
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                                          event_handler, NULL, NULL));
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

static esp_err_t start_host(void)
{
    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    if (ap_netif) {
        esp_netif_ip_info_t ip_info;
        memset(&ip_info, 0, sizeof(ip_info));
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
    ESP_LOGI(TAG, "host mode started: ssid=%s", s_status.ssid);
    return ESP_OK;
}

static esp_err_t scan_for_host(char out_ssid[33])
{
    wifi_scan_config_t scan_cfg = {
        .ssid = NULL,
        .show_hidden = false,
    };
    ESP_RETURN_ON_ERROR(esp_wifi_scan_start(&scan_cfg, true), TAG, "scan");

    uint16_t ap_count = 0;
    ESP_RETURN_ON_ERROR(esp_wifi_scan_get_ap_num(&ap_count), TAG, "scan count");
    if (ap_count == 0) {
        return ESP_ERR_NOT_FOUND;
    }
    if (ap_count > 16) {
        ap_count = 16;
    }

    wifi_ap_record_t records[16] = {0};
    ESP_RETURN_ON_ERROR(esp_wifi_scan_get_ap_records(&ap_count, records), TAG, "scan records");
    int best = -1;
    int best_rssi = -127;
    for (uint16_t i = 0; i < ap_count; i++) {
        const char *ssid = (const char *)records[i].ssid;
        if (strncmp(ssid, WIFI_LINK_SOFTAP_PREFIX, strlen(WIFI_LINK_SOFTAP_PREFIX)) == 0 &&
            records[i].rssi > best_rssi) {
            best = (int)i;
            best_rssi = records[i].rssi;
        }
    }
    if (best < 0) {
        return ESP_ERR_NOT_FOUND;
    }
    memcpy(out_ssid, records[best].ssid, 32);
    out_ssid[32] = '\0';
    return ESP_OK;
}

static esp_err_t connect_to_host(const char *host_ssid)
{
    snprintf(s_status.ssid, sizeof(s_status.ssid), "%s", host_ssid);

    wifi_config_t cfg = {0};
    copy_wifi_bytes(cfg.sta.ssid, sizeof(cfg.sta.ssid), host_ssid);
    copy_wifi_bytes(cfg.sta.password, sizeof(cfg.sta.password), WIFI_LINK_PASSWORD);
    cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    cfg.sta.pmf_cfg.capable = true;
    cfg.sta.pmf_cfg.required = false;

    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &cfg), TAG, "set STA config");
    ESP_RETURN_ON_ERROR(esp_wifi_connect(), TAG, "connect");
    ESP_LOGI(TAG, "join mode connecting to %s", host_ssid);
    return ESP_OK;
}

static void join_retry_task(void *arg)
{
    (void)arg;
    while (!s_status.connected) {
        char host_ssid[33] = {0};
        s_status.scanning = true;
        s_status.retry_count++;
        esp_err_t rc = scan_for_host(host_ssid);
        if (rc == ESP_OK) {
            s_status.scanning = false;
            rc = connect_to_host(host_ssid);
            s_status.last_error = rc;
            if (rc == ESP_OK) {
                break;
            }
        } else {
            s_status.last_error = rc;
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    s_status.scanning = false;
    s_join_task = NULL;
    vTaskDelete(NULL);
}

static esp_err_t start_join(void)
{
    esp_netif_create_default_wifi_sta();
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set STA mode");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start STA");
    s_status.scanning = true;
    if (!s_join_task &&
        xTaskCreate(join_retry_task, "wifi_join", 4096, NULL, 3, &s_join_task) != pdPASS) {
        s_status.scanning = false;
        s_status.last_error = ESP_ERR_NO_MEM;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "join mode scanning for %s*", WIFI_LINK_SOFTAP_PREFIX);
    return ESP_OK;
}

esp_err_t wifi_link_init(wifi_link_mode_t mode)
{
    if (mode < WIFI_LINK_MODE_OFF || mode > WIFI_LINK_MODE_JOIN) {
        mode = WIFI_LINK_MODE_OFF;
    }
    s_status.mode = mode;
    s_status.connected = false;
    s_status.ap_clients = 0;
    s_status.scanning = false;
    s_status.retry_count = 0;
    s_status.last_error = ESP_OK;

    if (mode == WIFI_LINK_MODE_OFF) {
        make_identity();
        ESP_LOGI(TAG, "CDJ Link wireless disabled");
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(ensure_wifi_stack(), TAG, "wireless stack");
    ESP_RETURN_ON_ERROR(make_identity(), TAG, "wireless identity");
    esp_err_t rc = (mode == WIFI_LINK_MODE_HOST) ? start_host() : start_join();
    s_status.initialized = (rc == ESP_OK);
    s_status.last_error = rc;
    return rc;
}

wifi_link_status_t wifi_link_get_status(void)
{
    return s_status;
}

const char *wifi_link_peer_id(void)
{
    return s_status.peer_id;
}

const char *wifi_link_ssid(void)
{
    return s_status.ssid;
}
