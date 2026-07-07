#include "s3_debug_ap.h"

#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifdef S3_DEBUG_AP_PC_TEST

static s3_debug_ap_status_t s_status;
static s3_debug_ap_status_cb_t s_status_cb;
static esp_err_t s_test_start_result = ESP_OK;

static void set_status(s3_debug_ap_status_t status)
{
    s_status = status;
    if (s_status_cb) {
        s_status_cb(status);
    }
}

void s3_debug_ap_test_set_start_result(esp_err_t result)
{
    s_test_start_result = result;
}

void s3_debug_ap_test_reset(void)
{
    s_status = S3_DEBUG_AP_STATUS_OFF;
    s_status_cb = NULL;
    s_test_start_result = ESP_OK;
}

esp_err_t s3_debug_ap_init(void)
{
    s_status = S3_DEBUG_AP_STATUS_OFF;
    return ESP_OK;
}

esp_err_t s3_debug_ap_set_status_callback(s3_debug_ap_status_cb_t cb)
{
    s_status_cb = cb;
    return ESP_OK;
}

esp_err_t s3_debug_ap_request(bool enable)
{
    if (!enable) {
        set_status(S3_DEBUG_AP_STATUS_OFF);
        return ESP_OK;
    }

    set_status(S3_DEBUG_AP_STATUS_STARTING);
    if (s_test_start_result != ESP_OK) {
        set_status(S3_DEBUG_AP_STATUS_ERROR);
        return s_test_start_result;
    }
    set_status(S3_DEBUG_AP_STATUS_ON);
    return ESP_OK;
}

s3_debug_ap_status_t s3_debug_ap_status(void)
{
    return s_status;
}

#else

#include "sdkconfig.h"

#if CONFIG_S3_DEBUG_AP_ENABLED

#include <stdatomic.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static const char *TAG = "s3_debug_ap";

static s3_debug_ap_status_t s_status = S3_DEBUG_AP_STATUS_OFF;
static s3_debug_ap_status_cb_t s_status_cb;
static s3_debug_log_ring_t s_log_ring;
static SemaphoreHandle_t s_lock;
static httpd_handle_t s_httpd;
static esp_netif_t *s_ap_netif;
static vprintf_like_t s_prev_vprintf;
static atomic_bool s_log_hook_active;
static bool s_wifi_initialized;
static bool s_wifi_started;

static void set_status(s3_debug_ap_status_t status)
{
    s_status = status;
    if (s_status_cb) {
        s_status_cb(status);
    }
}

static int s3_debug_ap_vprintf(const char *fmt, va_list args)
{
    va_list uart_args;
    va_copy(uart_args, args);
    int ret = s_prev_vprintf ? s_prev_vprintf(fmt, uart_args) : vprintf(fmt, uart_args);
    va_end(uart_args);

    if (atomic_load(&s_log_hook_active)) {
        char line[S3_DEBUG_LOG_LINE_MAX];
        va_list copy_args;
        va_copy(copy_args, args);
        int len = vsnprintf(line, sizeof(line), fmt, copy_args);
        va_end(copy_args);
        if (len > 0 && s_lock && xSemaphoreTake(s_lock, 0) == pdTRUE) {
            s3_debug_log_ring_append(&s_log_ring, line);
            xSemaphoreGive(s_lock);
        }
    }
    return ret;
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
    static const char html[] =
        "<!doctype html><html><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>S3 Debug Log</title>"
        "<style>body{margin:0;background:#101217;color:#e7eaf0;font:14px monospace;}"
        "header{padding:12px 16px;background:#1c2029;border-bottom:1px solid #333946;}"
        "#log{white-space:pre-wrap;padding:12px 16px;}</style></head>"
        "<body><header><strong>S3 Debug Log</strong><br>"
        "PajoNiiiR-S3-DEBUG / http://192.168.4.1</header><main id=\"log\"></main>"
        "<script>const log=document.getElementById('log');"
        "function start(){const es=new EventSource('/events');"
        "es.onmessage=e=>{log.textContent+=e.data+'\\n';window.scrollTo(0,document.body.scrollHeight);};"
        "es.onerror=()=>{es.close();setTimeout(start,1000);};}start();</script></body></html>";
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t events_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/event-stream");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");

    char snapshot[2048];
    for (int i = 0; i < 120; i++) {
        snapshot[0] = '\0';
        if (s_lock && xSemaphoreTake(s_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
            (void)s3_debug_log_ring_snapshot(&s_log_ring, snapshot, sizeof(snapshot), 0);
            xSemaphoreGive(s_lock);
        }
        if (snapshot[0] != '\0') {
            if (httpd_resp_sendstr_chunk(req, "data: ") != ESP_OK) {
                break;
            }
            for (char *p = snapshot; *p; p++) {
                if (*p == '\n') {
                    if (httpd_resp_sendstr_chunk(req, "\ndata: ") != ESP_OK) {
                        return ESP_FAIL;
                    }
                } else {
                    char c[2] = { *p, '\0' };
                    if (httpd_resp_sendstr_chunk(req, c) != ESP_OK) {
                        return ESP_FAIL;
                    }
                }
            }
            if (httpd_resp_sendstr_chunk(req, "\n\n") != ESP_OK) {
                break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    return httpd_resp_sendstr_chunk(req, NULL);
}

static esp_err_t start_httpd(void)
{
    if (s_httpd) {
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    ESP_RETURN_ON_ERROR(httpd_start(&s_httpd, &config), TAG, "httpd_start");

    httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_get_handler,
    };
    httpd_uri_t events = {
        .uri = "/events",
        .method = HTTP_GET,
        .handler = events_get_handler,
    };
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &root), TAG, "root handler");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &events), TAG, "events handler");
    return ESP_OK;
}

static esp_err_t init_nvs(void)
{
    esp_err_t rc = nvs_flash_init();
    if (rc == ESP_ERR_NVS_NO_FREE_PAGES || rc == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "erase NVS");
        rc = nvs_flash_init();
    }
    return rc;
}

static esp_err_t configure_ap_ip(void)
{
    esp_netif_ip_info_t ip_info;
    ESP_RETURN_ON_ERROR(esp_netif_dhcps_stop(s_ap_netif), TAG, "stop AP DHCP");
    ESP_RETURN_ON_FALSE(esp_netif_str_to_ip4(S3_DEBUG_AP_IP, &ip_info.ip) == ESP_OK,
                        ESP_ERR_INVALID_ARG, TAG, "parse AP IP");
    ip_info.gw = ip_info.ip;
    ESP_RETURN_ON_FALSE(esp_netif_str_to_ip4("255.255.255.0", &ip_info.netmask) == ESP_OK,
                        ESP_ERR_INVALID_ARG, TAG, "parse AP netmask");
    ESP_RETURN_ON_ERROR(esp_netif_set_ip_info(s_ap_netif, &ip_info), TAG, "set AP IP");
    ESP_RETURN_ON_ERROR(esp_netif_dhcps_start(s_ap_netif), TAG, "start AP DHCP");
    return ESP_OK;
}

static esp_err_t start_ap(void)
{
    ESP_RETURN_ON_ERROR(init_nvs(), TAG, "nvs_flash_init");
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "esp_netif_init");

    esp_err_t loop_rc = esp_event_loop_create_default();
    if (loop_rc != ESP_OK && loop_rc != ESP_ERR_INVALID_STATE) {
        ESP_RETURN_ON_ERROR(loop_rc, TAG, "event loop");
    }

    if (!s_ap_netif) {
        s_ap_netif = esp_netif_create_default_wifi_ap();
        ESP_RETURN_ON_FALSE(s_ap_netif, ESP_FAIL, TAG, "create AP netif");
        ESP_RETURN_ON_ERROR(configure_ap_ip(), TAG, "configure AP IP");
    }

    if (!s_wifi_initialized) {
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "esp_wifi_init");
        s_wifi_initialized = true;
    }

    wifi_config_t ap_config = { 0 };
    snprintf((char *)ap_config.ap.ssid, sizeof(ap_config.ap.ssid), "%s", S3_DEBUG_AP_SSID);
    ap_config.ap.ssid_len = strlen(S3_DEBUG_AP_SSID);
    ap_config.ap.channel = 1;
    ap_config.ap.max_connection = CONFIG_S3_DEBUG_AP_MAX_CLIENTS;
    ap_config.ap.authmode = WIFI_AUTH_OPEN;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG, "esp_wifi_set_mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap_config), TAG, "esp_wifi_set_config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "esp_wifi_start");
    s_wifi_started = true;

    return start_httpd();
}

static void stop_ap(void)
{
    atomic_store(&s_log_hook_active, false);
    if (s_httpd) {
        httpd_stop(s_httpd);
        s_httpd = NULL;
    }
    if (s_wifi_started) {
        (void)esp_wifi_stop();
        s_wifi_started = false;
    }
}

esp_err_t s3_debug_ap_init(void)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        ESP_RETURN_ON_FALSE(s_lock, ESP_ERR_NO_MEM, TAG, "create lock");
    }
    s3_debug_log_ring_init(&s_log_ring);
    s_status = S3_DEBUG_AP_STATUS_OFF;
    return ESP_OK;
}

esp_err_t s3_debug_ap_set_status_callback(s3_debug_ap_status_cb_t cb)
{
    s_status_cb = cb;
    return ESP_OK;
}

esp_err_t s3_debug_ap_request(bool enable)
{
    if (!enable) {
        stop_ap();
        set_status(S3_DEBUG_AP_STATUS_OFF);
        return ESP_OK;
    }

    set_status(S3_DEBUG_AP_STATUS_STARTING);
    if (!s_prev_vprintf) {
        s_prev_vprintf = esp_log_set_vprintf(s3_debug_ap_vprintf);
    }
    atomic_store(&s_log_hook_active, true);

    esp_err_t rc = start_ap();
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "debug AP start failed: %s", esp_err_to_name(rc));
        stop_ap();
        set_status(S3_DEBUG_AP_STATUS_ERROR);
        return rc;
    }

    ESP_LOGI(TAG, "S3 debug AP active: SSID=%s URL=http://%s", S3_DEBUG_AP_SSID, S3_DEBUG_AP_IP);
    set_status(S3_DEBUG_AP_STATUS_ON);
    return ESP_OK;
}

s3_debug_ap_status_t s3_debug_ap_status(void)
{
    return s_status;
}

#else

esp_err_t s3_debug_ap_init(void)
{
    return ESP_OK;
}

esp_err_t s3_debug_ap_set_status_callback(s3_debug_ap_status_cb_t cb)
{
    (void)cb;
    return ESP_OK;
}

esp_err_t s3_debug_ap_request(bool enable)
{
    return enable ? ESP_ERR_NOT_SUPPORTED : ESP_OK;
}

s3_debug_ap_status_t s3_debug_ap_status(void)
{
    return S3_DEBUG_AP_STATUS_OFF;
}

#endif /* CONFIG_S3_DEBUG_AP_ENABLED */

#endif
