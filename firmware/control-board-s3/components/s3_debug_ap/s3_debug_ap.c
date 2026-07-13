#include "s3_debug_ap.h"

#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
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
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "s3_ota.h"
#include "s3_ota_policy.h"

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

/* AP bring-up/teardown runs in a dedicated worker task, not on the caller
 * (control-link RX task): the WiFi/httpd start takes hundreds of ms, long
 * enough to overflow the 256-byte UART RX ring and drop inbound P4 frames.
 * s_ap_desired/s_ap_active are guarded by s_lock; the worker collapses rapid
 * toggles by looping until active == desired. */
static volatile bool s_ap_desired;
static volatile bool s_ap_active;
static bool s_ap_worker_running;

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
        "PajoNiiiR-S3-DEBUG / http://192.168.4.1<br>"
        "<a href=\"/update\" style=\"color:#7fc7ff\">Firmware Update</a></header>"
        "<main id=\"log\"></main>"
        "<script>const log=document.getElementById('log');"
        "function start(){const es=new EventSource('/events');"
        "es.onmessage=e=>{log.textContent+=e.data+'\\n';window.scrollTo(0,document.body.scrollHeight);};"
        "es.onerror=()=>{es.close();setTimeout(start,1000);};}start();</script></body></html>";
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t events_send_sse(httpd_req_t *req, char *text)
{
    // Emit one SSE event whose payload is `text`, mapping each embedded
    // newline to a fresh "data:" line so multi-line log blocks render intact.
    if (httpd_resp_sendstr_chunk(req, "data: ") != ESP_OK) {
        return ESP_FAIL;
    }
    char *start = text;
    for (char *p = text;; p++) {
        if (*p == '\n' || *p == '\0') {
            char saved = *p;
            *p = '\0';
            if (httpd_resp_sendstr_chunk(req, start) != ESP_OK) {
                return ESP_FAIL;
            }
            *p = saved;
            if (saved == '\0') {
                break;
            }
            if (httpd_resp_sendstr_chunk(req, "\ndata: ") != ESP_OK) {
                return ESP_FAIL;
            }
            start = p + 1;
        }
    }
    return httpd_resp_sendstr_chunk(req, "\n\n");
}

static esp_err_t events_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/event-stream");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");

    // Stream only lines newer than what this client has already received.
    uint32_t last_seq = 0;
    char chunk[512];
    for (int i = 0; i < 600; i++) {
        chunk[0] = '\0';
        uint32_t seen_seq = last_seq;
        if (s_lock && xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
            (void)s3_debug_log_ring_snapshot(&s_log_ring, chunk, sizeof(chunk), last_seq);
            seen_seq = s_log_ring.next_seq;
            xSemaphoreGive(s_lock);
        }
        if (chunk[0] != '\0') {
            last_seq = seen_seq;
            if (events_send_sse(req, chunk) != ESP_OK) {
                break;
            }
        } else if (httpd_resp_sendstr_chunk(req, ": keepalive\n\n") != ESP_OK) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    return httpd_resp_sendstr_chunk(req, NULL);
}

static esp_err_t update_get_handler(httpd_req_t *req)
{
    static const char html[] =
        "<!doctype html><html><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>S3 Firmware Update</title>"
        "<style>body{max-width:680px;margin:32px auto;padding:0 16px;background:#101217;"
        "color:#e7eaf0;font:15px system-ui}section{padding:20px;background:#1c2029;"
        "border:1px solid #333946;border-radius:10px}button,input{margin-top:12px}"
        "button{padding:10px 16px}progress{width:100%;margin-top:16px}"
        "#msg{white-space:pre-wrap;margin-top:12px}.warn{color:#ffcb6b}</style></head>"
        "<body><section><a href=\"/\" style=\"color:#7fc7ff\">Back to log</a>"
        "<h1>S3 Firmware Update</h1><p id=\"status\">Loading status...</p>"
        "<p class=\"warn\">Upload only the signed control-board-s3 .ddjota bundle. "
        "The controller reboots and the Debug AP turns off after success.</p>"
        "<input id=\"file\" type=\"file\" accept=\".ddjota,application/octet-stream\"><br>"
        "<button id=\"upload\">Upload and reboot</button>"
        "<progress id=\"progress\" value=\"0\" max=\"100\"></progress>"
        "<div id=\"msg\"></div></section><script>"
        "const statusEl=document.getElementById('status'),msg=document.getElementById('msg'),"
        "progress=document.getElementById('progress'),button=document.getElementById('upload');"
        "async function refresh(){try{const r=await fetch('/api/firmware',{cache:'no-store'});"
        "const s=await r.json();statusEl.textContent='Running: '+s.running_slot+' / '+"
        "s.running_version+' | State: '+s.state+(s.last_error?' | '+s.last_error:'');}"
        "catch(e){statusEl.textContent='Status unavailable: '+e.message;}}refresh();"
        "button.onclick=()=>{const file=document.getElementById('file').files[0];"
        "if(!file){msg.textContent='Select a signed .ddjota bundle first.';return;}"
        "if(!file.name.toLowerCase().endsWith('.ddjota')){msg.textContent="
        "'Unsigned .bin images are rejected. Select the S3 .ddjota bundle.';return;}"
        "if(!confirm('Upload S3 firmware and reboot the controller?'))return;"
        "button.disabled=true;msg.textContent='Uploading...';const x=new XMLHttpRequest();"
        "x.open('POST','/api/ota/s3');x.setRequestHeader('Content-Type','application/octet-stream');"
        "x.setRequestHeader('X-DDJ-OTA','s3');x.upload.onprogress=e=>{if(e.lengthComputable)"
        "progress.value=Math.round(e.loaded*100/e.total);};"
        "x.onload=()=>{msg.textContent=x.status>=200&&x.status<300?"
        "'Signature and image accepted. S3 is rebooting; reconnect through P4 Settings.':"
        "'Upload failed ('+x.status+'): '+x.responseText;if(x.status<200||x.status>=300)"
        "button.disabled=false;};x.onerror=()=>{msg.textContent='Connection failed during upload.';"
        "button.disabled=false;};x.send(file);};</script></body></html>";
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t firmware_get_handler(httpd_req_t *req)
{
    s3_ota_status_t status;
    s3_ota_get_status(&status);
    char json[384];
    int len = snprintf(json, sizeof(json),
                       "{\"target\":\"s3\",\"state\":\"%s\","
                       "\"running_slot\":\"%s\",\"running_version\":\"%s\","
                       "\"target_slot\":\"%s\",\"target_version\":\"%s\","
                       "\"expected_size\":%u,\"received_size\":%u,"
                       "\"last_error\":\"%s\"}",
                       s3_ota_state_name(status.state),
                       status.running_slot, status.running_version,
                       status.target_slot, status.target_version,
                       (unsigned)status.expected_size,
                       (unsigned)status.received_size,
                       status.last_error);
    if (len < 0 || (size_t)len >= sizeof(json)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "OTA status overflow");
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, json, (size_t)len);
}

static int ota_http_recv(httpd_req_t *req, uint8_t *buffer, size_t wanted)
{
    const unsigned max_timeouts = 5;
    for (unsigned timeout_count = 0; timeout_count < max_timeouts; ++timeout_count) {
        int received = httpd_req_recv(req, (char *)buffer, wanted);
        if (received != HTTPD_SOCK_ERR_TIMEOUT) return received;
    }
    return HTTPD_SOCK_ERR_TIMEOUT;
}

static esp_err_t ota_receive_error(httpd_req_t *req, int received, bool started)
{
    if (started) s3_ota_abort("HTTP upload interrupted");
    if (received == HTTPD_SOCK_ERR_TIMEOUT) {
        httpd_resp_set_status(req, "408 Request Timeout");
        return httpd_resp_send(req, "Firmware upload timed out", HTTPD_RESP_USE_STRLEN);
    }
    return ESP_FAIL;
}

static void ota_restart_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

static esp_err_t ota_post_handler(httpd_req_t *req)
{
    char target[8] = {0};
    if (httpd_req_get_hdr_value_str(req, "X-DDJ-OTA", target, sizeof(target)) != ESP_OK ||
        strcmp(target, "s3") != 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing X-DDJ-OTA: s3");
    }
    if (req->content_len < DDJ_OTA_HEADER_SIZE + S3_OTA_IMAGE_HEADER_SIZE) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "Signed OTA bundle is too small");
    }

    uint8_t *buffer = malloc(4096);
    if (!buffer) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No memory");
    }

    uint8_t manifest_header[DDJ_OTA_HEADER_SIZE];
    size_t manifest_received = 0;
    while (manifest_received < sizeof(manifest_header)) {
        int received = ota_http_recv(req, manifest_header + manifest_received,
                                     sizeof(manifest_header) - manifest_received);
        if (received <= 0) {
            free(buffer);
            return ota_receive_error(req, received, false);
        }
        manifest_received += (size_t)received;
    }

    ddj_ota_manifest_t manifest;
    ddj_ota_manifest_result_t manifest_rc = ddj_ota_manifest_parse(
        manifest_header, sizeof(manifest_header), DDJ_OTA_TARGET_S3,
        S3_OTA_ESP32S3_CHIP_ID, "control-board-s3", S3_OTA_MAX_IMAGE_SIZE,
        &manifest);
    if (manifest_rc != DDJ_OTA_MANIFEST_OK) {
        free(buffer);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   ddj_ota_manifest_result_name(manifest_rc));
    }
    if (!ddj_ota_manifest_verify_signature(manifest_header, sizeof(manifest_header))) {
        free(buffer);
        httpd_resp_set_status(req, "403 Forbidden");
        return httpd_resp_send(req, "Invalid OTA manifest signature", HTTPD_RESP_USE_STRLEN);
    }
    if ((size_t)req->content_len != DDJ_OTA_HEADER_SIZE + manifest.image_size) {
        free(buffer);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "Bundle length does not match signed manifest");
    }

    size_t remaining = manifest.image_size;
    size_t buffered = 0;
    while (buffered < S3_OTA_IMAGE_HEADER_SIZE) {
        size_t wanted = remaining < 4096u - buffered ? remaining : 4096u - buffered;
        int received = ota_http_recv(req, buffer + buffered, wanted);
        if (received <= 0) {
            free(buffer);
            return ota_receive_error(req, received, false);
        }
        buffered += (size_t)received;
        remaining -= (size_t)received;
    }
    if (!s3_ota_policy_header_valid(buffer, buffered)) {
        free(buffer);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "Not an ESP32-S3 firmware image");
    }

    esp_err_t rc = s3_ota_begin(&manifest);
    if (rc != ESP_OK) {
        free(buffer);
        httpd_resp_set_status(req, rc == ESP_ERR_INVALID_STATE ? "409 Conflict" :
                                                                  "400 Bad Request");
        return httpd_resp_send(req, esp_err_to_name(rc), HTTPD_RESP_USE_STRLEN);
    }
    rc = s3_ota_write(buffer, buffered);
    if (rc != ESP_OK) {
        free(buffer);
        s3_ota_abort(esp_err_to_name(rc));
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "Flash write failed");
    }

    while (remaining > 0) {
        size_t wanted = remaining < 4096u ? remaining : 4096u;
        int received = ota_http_recv(req, buffer, wanted);
        if (received <= 0) {
            free(buffer);
            return ota_receive_error(req, received, true);
        }
        rc = s3_ota_write(buffer, (size_t)received);
        if (rc != ESP_OK) {
            free(buffer);
            s3_ota_abort(esp_err_to_name(rc));
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                       "Flash write failed");
        }
        remaining -= (size_t)received;
    }
    free(buffer);

    rc = s3_ota_finish();
    if (rc != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "Firmware validation failed");
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Connection", "close");
    esp_err_t send_rc = httpd_resp_send(req, "{\"ok\":true,\"rebooting\":true}",
                                        HTTPD_RESP_USE_STRLEN);
    if (xTaskCreate(ota_restart_task, "ota_reboot", 2048, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "failed to create OTA reboot task");
        esp_restart();
    }
    return send_rc;
}

static esp_err_t start_httpd(void)
{
    if (s_httpd) {
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    // The SSE handler runs a long-lived loop with local buffers on top of the
    // httpd internals; the 4 KB default task stack overflows, so give it room.
    config.stack_size = 8192;
    config.lru_purge_enable = true;
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
    httpd_uri_t update = {
        .uri = "/update",
        .method = HTTP_GET,
        .handler = update_get_handler,
    };
    httpd_uri_t firmware = {
        .uri = "/api/firmware",
        .method = HTTP_GET,
        .handler = firmware_get_handler,
    };
    httpd_uri_t ota = {
        .uri = "/api/ota/s3",
        .method = HTTP_POST,
        .handler = ota_post_handler,
    };
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &root), TAG, "root handler");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &events), TAG, "events handler");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &update), TAG, "update handler");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &firmware), TAG, "firmware handler");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &ota), TAG, "OTA handler");
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
    snprintf((char *)ap_config.ap.password, sizeof(ap_config.ap.password), "%s",
             S3_DEBUG_AP_PASSWORD);
    ap_config.ap.ssid_len = strlen(S3_DEBUG_AP_SSID);
    ap_config.ap.channel = 1;
    ap_config.ap.max_connection = CONFIG_S3_DEBUG_AP_MAX_CLIENTS;
    ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap_config.ap.pmf_cfg.required = false;

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

/* Does the actual (blocking) WiFi/httpd bring-up or teardown. Only ever runs on
 * the worker task (or directly if init has not run yet). */
static esp_err_t s3_debug_ap_apply(bool enable)
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

static void s3_debug_ap_worker(void *arg)
{
    (void)arg;
    for (;;) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        bool desired = s_ap_desired;
        bool active = s_ap_active;
        if (desired == active) {
            s_ap_worker_running = false;
            xSemaphoreGive(s_lock);
            break;
        }
        xSemaphoreGive(s_lock);

        esp_err_t rc = s3_debug_ap_apply(desired);

        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_ap_active = desired ? (rc == ESP_OK) : false;
        xSemaphoreGive(s_lock);
    }
    vTaskDelete(NULL);
}

esp_err_t s3_debug_ap_request(bool enable)
{
    if (!s_lock) {
        /* init not run — fall back to a direct (blocking) apply. */
        esp_err_t rc = s3_debug_ap_apply(enable);
        s_ap_active = enable ? (rc == ESP_OK) : false;
        return rc;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_ap_desired = enable;
    bool spawn = !s_ap_worker_running;
    if (spawn) {
        s_ap_worker_running = true;
    }
    xSemaphoreGive(s_lock);

    if (spawn) {
        if (xTaskCreate(s3_debug_ap_worker, "s3dbgap", 4096, NULL, 3, NULL) != pdPASS) {
            ESP_LOGE(TAG, "failed to spawn s3 debug AP worker");
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_ap_worker_running = false;
            xSemaphoreGive(s_lock);
            return ESP_ERR_NO_MEM;
        }
    }
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
