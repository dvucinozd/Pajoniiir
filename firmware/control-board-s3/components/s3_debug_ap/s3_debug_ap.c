#include "s3_debug_ap.h"

#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef S3_DEBUG_AP_PC_TEST

static s3_debug_ap_status_t s_status;
static s3_debug_ap_status_cb_t s_status_cb;
static s3_debug_ap_token_cb_t s_token_cb;
static esp_err_t s_test_start_result = ESP_OK;
static bool s_test_start_failed_latched;

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
    s_token_cb = NULL;
    s_test_start_result = ESP_OK;
    s_test_start_failed_latched = false;
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

esp_err_t s3_debug_ap_set_token_callback(s3_debug_ap_token_cb_t cb)
{
    s_token_cb = cb;
    return ESP_OK;
}

esp_err_t s3_debug_ap_request(bool enable)
{
    if (!enable) {
        s_test_start_failed_latched = false;
        set_status(S3_DEBUG_AP_STATUS_OFF);
        if (s_token_cb) s_token_cb(0u);
        return ESP_OK;
    }

    if (s_test_start_failed_latched) return ESP_ERR_INVALID_STATE;

    set_status(S3_DEBUG_AP_STATUS_STARTING);
    if (s_test_start_result != ESP_OK) {
        s_test_start_failed_latched = true;
        set_status(S3_DEBUG_AP_STATUS_ERROR);
        return s_test_start_result;
    }
    if (s_token_cb) s_token_cb(123456u);
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
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "s3_ota.h"
#include "s3_ota_policy.h"
#include "s3_ota_upload_guard.h"
#include "s3_debug_ap_netif_stage.h"
#include "s3_debug_auth.h"

static const char *TAG = "s3_debug_ap";

static s3_debug_ap_status_t s_status = S3_DEBUG_AP_STATUS_OFF;
static s3_debug_ap_status_cb_t s_status_cb;
static s3_debug_ap_token_cb_t s_token_cb;
static s3_debug_log_ring_t s_log_ring;
static SemaphoreHandle_t s_lock;
static httpd_handle_t s_httpd;
static esp_netif_t *s_ap_netif;
static vprintf_like_t s_prev_vprintf;
static atomic_bool s_log_hook_active;
static bool s_wifi_initialized;
static bool s_wifi_started;
static esp_timer_handle_t s_idle_timer;
static s3_debug_auth_t s_maintenance_auth;

static uint32_t maintenance_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static bool s3_api_request_allowed(httpd_req_t *req, bool mutation)
{
    char host[32] = {0};
    if (httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) != ESP_OK ||
        (strcmp(host, S3_DEBUG_AP_IP) != 0 &&
         strcmp(host, S3_DEBUG_AP_IP ":80") != 0)) {
        httpd_resp_set_status(req, "403 Forbidden");
        (void)httpd_resp_send(req, "Invalid Host", HTTPD_RESP_USE_STRLEN);
        return false;
    }
    if (mutation) {
        char marker[4] = {0};
        if (httpd_req_get_hdr_value_str(req, "X-DDJ-Control", marker,
                                        sizeof(marker)) != ESP_OK ||
            strcmp(marker, "1") != 0) {
            httpd_resp_set_status(req, "403 Forbidden");
            (void)httpd_resp_send(req, "Missing X-DDJ-Control",
                                  HTTPD_RESP_USE_STRLEN);
            return false;
        }
        char token[S3_DEBUG_AUTH_TOKEN_DIGITS + 1u] = {0};
        if (httpd_req_get_hdr_value_str(req, "X-Pajoniiir-Maintenance",
                                        token, sizeof(token)) != ESP_OK) {
            httpd_resp_set_status(req, "401 Unauthorized");
            (void)httpd_resp_send(req, "Maintenance code required",
                                  HTTPD_RESP_USE_STRLEN);
            return false;
        }
        s3_debug_auth_result_t auth = s3_debug_auth_check(
            &s_maintenance_auth, token, maintenance_now_ms());
        if (auth != S3_DEBUG_AUTH_OK) {
            if (auth == S3_DEBUG_AUTH_RATE_LIMITED) {
                httpd_resp_set_status(req, "429 Too Many Requests");
                (void)httpd_resp_send(req,
                                      "Maintenance code locked; toggle Debug AP off and on",
                                      HTTPD_RESP_USE_STRLEN);
            } else {
                httpd_resp_set_status(req, "401 Unauthorized");
                (void)httpd_resp_send(req,
                                      auth == S3_DEBUG_AUTH_EXPIRED
                                          ? "Maintenance code expired; toggle Debug AP off and on"
                                          : "Invalid maintenance code",
                                      HTTPD_RESP_USE_STRLEN);
            }
            return false;
        }
    }
    return true;
}

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
        "Pajoniiir-S3-DEBUG / http://192.168.4.1<br>"
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
    if (!s3_api_request_allowed(req, false)) return ESP_FAIL;

    uint32_t last_seq = 0u;
    char query[48] = {0};
    char after[16] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
        httpd_query_key_value(query, "after", after, sizeof(after)) == ESP_OK) {
        char *end = NULL;
        unsigned long parsed = strtoul(after, &end, 10);
        if (end && *end == '\0' && parsed <= UINT32_MAX) {
            last_seq = (uint32_t)parsed;
        }
    }

    char chunk[512] = {0};
    uint32_t next_seq = last_seq;
    if (s_lock && xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
        (void)s3_debug_log_ring_snapshot(&s_log_ring, chunk, sizeof(chunk), last_seq);
        next_seq = s_log_ring.next_seq;
        xSemaphoreGive(s_lock);
    }

    char seq_header[16];
    snprintf(seq_header, sizeof(seq_header), "%u", (unsigned)next_seq);
    httpd_resp_set_type(req, "text/event-stream");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store");
    httpd_resp_set_hdr(req, "X-Log-Seq", seq_header);
    httpd_resp_set_hdr(req, "Connection", "close");

    /* One bounded response per request, resumed by the client through ?after=.
     * The previous handler looped for up to ten minutes inside the request,
     * and ESP-IDF's httpd is synchronous: a single browser tab watching the log
     * therefore occupied the server task and blocked /update, /api/firmware and
     * the OTA upload for as long as it stayed open. EventSource reconnects on
     * its own once this response closes. */
    if (chunk[0] != '\0' && events_send_sse(req, chunk) != ESP_OK) {
        return ESP_FAIL;
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
        "<label>Maintenance code shown on P4 Settings<br>"
        "<input id=\"token\" inputmode=\"numeric\" pattern=\"[0-9]{6}\" maxlength=\"6\" "
        "autocomplete=\"one-time-code\"></label><br>"
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
        "button.onclick=()=>{const file=document.getElementById('file').files[0],"
        "token=document.getElementById('token').value;"
        "if(!/^[0-9]{6}$/.test(token)){msg.textContent='Enter the 6-digit code from P4 Settings.';return;}"
        "if(!file){msg.textContent='Select a signed .ddjota bundle first.';return;}"
        "if(!file.name.toLowerCase().endsWith('.ddjota')){msg.textContent="
        "'Unsigned .bin images are rejected. Select the S3 .ddjota bundle.';return;}"
        "if(!confirm('Upload S3 firmware and reboot the controller?'))return;"
        "button.disabled=true;msg.textContent='Uploading...';const x=new XMLHttpRequest();"
        "x.open('POST','/api/ota/s3');x.setRequestHeader('Content-Type','application/octet-stream');"
        "x.setRequestHeader('X-DDJ-Control','1');"
        "x.setRequestHeader('X-Pajoniiir-Maintenance',token);"
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
    if (!s3_api_request_allowed(req, false)) return ESP_FAIL;
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

static uint32_t ota_now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static int ota_http_recv(httpd_req_t *req, uint8_t *buffer, size_t wanted,
                         s3_ota_upload_guard_t *guard,
                         s3_ota_upload_guard_result_t *guard_result)
{
    const unsigned max_timeouts = 5;
    if (guard_result) *guard_result = S3_OTA_UPLOAD_GUARD_OK;
    for (unsigned timeout_count = 0; timeout_count < max_timeouts; ++timeout_count) {
        s3_ota_upload_guard_result_t check =
            s3_ota_upload_guard_check(guard, ota_now_ms());
        if (check != S3_OTA_UPLOAD_GUARD_OK) {
            if (guard_result) *guard_result = check;
            return HTTPD_SOCK_ERR_TIMEOUT;
        }
        int received = httpd_req_recv(req, (char *)buffer, wanted);
        if (received > 0) {
            s3_ota_upload_guard_note_bytes(guard, (size_t)received);
            check = s3_ota_upload_guard_check(guard, ota_now_ms());
            if (check != S3_OTA_UPLOAD_GUARD_OK) {
                if (guard_result) *guard_result = check;
                return HTTPD_SOCK_ERR_TIMEOUT;
            }
            return received;
        }
        if (received != HTTPD_SOCK_ERR_TIMEOUT) return received;
    }
    return HTTPD_SOCK_ERR_TIMEOUT;
}

static esp_err_t ota_receive_error(httpd_req_t *req, int received, bool started,
                                   s3_ota_upload_guard_result_t guard_result)
{
    if (started) s3_ota_abort("HTTP upload interrupted");
    if (received == HTTPD_SOCK_ERR_TIMEOUT) {
        ESP_LOGW(TAG, "OTA upload stopped: %s",
                 s3_ota_upload_guard_result_name(guard_result));
        httpd_resp_set_status(req, "408 Request Timeout");
        const char *detail = guard_result == S3_OTA_UPLOAD_GUARD_TOO_SLOW
            ? "Firmware upload throughput too low"
            : "Firmware upload timed out";
        return httpd_resp_send(req, detail, HTTPD_RESP_USE_STRLEN);
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
    if (!s3_api_request_allowed(req, true)) return ESP_FAIL;
    char target[8] = {0};
    if (httpd_req_get_hdr_value_str(req, "X-DDJ-OTA", target, sizeof(target)) != ESP_OK ||
        strcmp(target, "s3") != 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing X-DDJ-OTA: s3");
    }
    if (req->content_len < DDJ_OTA_HEADER_SIZE + S3_OTA_IMAGE_HEADER_SIZE) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "Signed OTA bundle is too small");
    }
    if ((size_t)req->content_len >
        DDJ_OTA_HEADER_SIZE + (size_t)S3_OTA_MAX_IMAGE_SIZE) {
        httpd_resp_set_status(req, "413 Payload Too Large");
        return httpd_resp_send(req, "Signed OTA bundle is too large",
                               HTTPD_RESP_USE_STRLEN);
    }

    s3_ota_upload_guard_t upload_guard;
    s3_ota_upload_guard_init(&upload_guard, ota_now_ms());
    s3_ota_upload_guard_result_t guard_result = S3_OTA_UPLOAD_GUARD_OK;

    uint8_t *buffer = malloc(4096);
    if (!buffer) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No memory");
    }

    uint8_t manifest_header[DDJ_OTA_HEADER_SIZE];
    size_t manifest_received = 0;
    while (manifest_received < sizeof(manifest_header)) {
        int received = ota_http_recv(req, manifest_header + manifest_received,
                                     sizeof(manifest_header) - manifest_received,
                                     &upload_guard, &guard_result);
        if (received <= 0) {
            free(buffer);
            return ota_receive_error(req, received, false, guard_result);
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
        int received = ota_http_recv(req, buffer + buffered, wanted,
                                     &upload_guard, &guard_result);
        if (received <= 0) {
            free(buffer);
            return ota_receive_error(req, received, false, guard_result);
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
        int received = ota_http_recv(req, buffer, wanted, &upload_guard,
                                     &guard_result);
        if (received <= 0) {
            free(buffer);
            return ota_receive_error(req, received, true, guard_result);
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
    httpd_uri_t *uris[] = { &root, &events, &update, &firmware, &ota };
    for (size_t i = 0u; i < sizeof(uris) / sizeof(uris[0]); i++) {
        esp_err_t rc = httpd_register_uri_handler(s_httpd, uris[i]);
        if (rc != ESP_OK) {
            ESP_LOGE(TAG, "URI handler registration failed for %s: %s",
                     uris[i]->uri, esp_err_to_name(rc));
            httpd_stop(s_httpd);
            s_httpd = NULL;
            return rc;
        }
    }
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

typedef struct {
    esp_netif_ip_info_t ip_info;
} ap_netif_stage_ctx_t;

static void *ap_netif_create(void *ctx)
{
    (void)ctx;
    return esp_netif_create_default_wifi_ap();
}

static esp_err_t ap_netif_stop_dhcp(void *resource, void *ctx)
{
    (void)ctx;
    return esp_netif_dhcps_stop((esp_netif_t *)resource);
}

static esp_err_t ap_netif_set_ip(void *resource, void *ctx)
{
    const ap_netif_stage_ctx_t *stage = (const ap_netif_stage_ctx_t *)ctx;
    return esp_netif_set_ip_info((esp_netif_t *)resource, &stage->ip_info);
}

static esp_err_t ap_netif_start_dhcp(void *resource, void *ctx)
{
    (void)ctx;
    return esp_netif_dhcps_start((esp_netif_t *)resource);
}

static void ap_netif_destroy(void *resource, void *ctx)
{
    (void)ctx;
    esp_netif_destroy_default_wifi(resource);
}

static esp_err_t ensure_ap_netif(void)
{
    ap_netif_stage_ctx_t stage = {0};
    ESP_RETURN_ON_FALSE(esp_netif_str_to_ip4(S3_DEBUG_AP_IP,
                                             &stage.ip_info.ip) == ESP_OK,
                        ESP_ERR_INVALID_ARG, TAG, "parse AP IP");
    stage.ip_info.gw = stage.ip_info.ip;
    ESP_RETURN_ON_FALSE(esp_netif_str_to_ip4("255.255.255.0",
                                             &stage.ip_info.netmask) == ESP_OK,
                        ESP_ERR_INVALID_ARG, TAG, "parse AP netmask");

    s3_debug_ap_netif_ops_t ops = {
        .create = ap_netif_create,
        .stop_dhcp = ap_netif_stop_dhcp,
        .set_ip = ap_netif_set_ip,
        .start_dhcp = ap_netif_start_dhcp,
        .destroy = ap_netif_destroy,
        .ctx = &stage,
    };
    void *published = s_ap_netif;
    esp_err_t rc = s3_debug_ap_netif_ensure(&published, &ops);
    if (rc == ESP_OK) s_ap_netif = (esp_netif_t *)published;
    return rc;
}

static esp_err_t start_ap(void)
{
    ESP_RETURN_ON_ERROR(init_nvs(), TAG, "nvs_flash_init");
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "esp_netif_init");

    esp_err_t loop_rc = esp_event_loop_create_default();
    if (loop_rc != ESP_OK && loop_rc != ESP_ERR_INVALID_STATE) {
        ESP_RETURN_ON_ERROR(loop_rc, TAG, "event loop");
    }

    ESP_RETURN_ON_ERROR(ensure_ap_netif(), TAG, "configure AP netif");

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

static void debug_ap_idle_timer_cb(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "S3 debug AP idle lifetime expired; requesting shutdown");
    (void)s3_debug_ap_request(false);
}

static void stop_ap(void)
{
    if (s_idle_timer) (void)esp_timer_stop(s_idle_timer);
    atomic_store(&s_log_hook_active, false);
    if (s_httpd) {
        httpd_stop(s_httpd);
        s_httpd = NULL;
    }
    if (s_wifi_started) {
        (void)esp_wifi_stop();
        s_wifi_started = false;
    }
    s3_debug_auth_init(&s_maintenance_auth, 0u, maintenance_now_ms());
    if (s_token_cb) s_token_cb(0u);
}

esp_err_t s3_debug_ap_init(void)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        ESP_RETURN_ON_FALSE(s_lock, ESP_ERR_NO_MEM, TAG, "create lock");
    }
    if (!s_idle_timer) {
        const esp_timer_create_args_t timer_args = {
            .callback = debug_ap_idle_timer_cb,
            .name = "s3dbgap_idle",
        };
        ESP_RETURN_ON_ERROR(esp_timer_create(&timer_args, &s_idle_timer),
                            TAG, "create idle timer");
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

esp_err_t s3_debug_ap_set_token_callback(s3_debug_ap_token_cb_t cb)
{
    s_token_cb = cb;
    return ESP_OK;
}

/* Does the actual (blocking) WiFi/httpd bring-up or teardown. Only ever runs on
 * the worker task (or directly if init has not run yet). */
/* ERROR is terminal for an ON edge until an explicit OFF clears it. */
static bool s_ap_start_failed_latched;

static esp_err_t s3_debug_ap_apply(bool enable)
{
    if (!enable) {
        stop_ap();
        set_status(S3_DEBUG_AP_STATUS_OFF);
        return ESP_OK;
    }

    set_status(S3_DEBUG_AP_STATUS_STARTING);
    uint32_t token = S3_DEBUG_AUTH_TOKEN_MIN +
                     (esp_random() % S3_DEBUG_AUTH_TOKEN_RANGE);
    s3_debug_auth_init(&s_maintenance_auth, token, maintenance_now_ms());
    if (s_token_cb) s_token_cb(token);
    if (!s_prev_vprintf) {
        s_prev_vprintf = esp_log_set_vprintf(s3_debug_ap_vprintf);
    }
    atomic_store(&s_log_hook_active, true);

    esp_err_t rc = start_ap();
    if (rc == ESP_OK && s_httpd) {
        /* start_ap() brings up netif/Wi-Fi and starts the server. Restart it
         * here so the bounded /events handler is registered before any client
         * can open a request. */
        httpd_stop(s_httpd);
        s_httpd = NULL;
        rc = start_httpd();
    }
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "debug AP start failed: %s", esp_err_to_name(rc));
        stop_ap();
        set_status(S3_DEBUG_AP_STATUS_ERROR);
        return rc;
    }

    if (s_idle_timer) {
        rc = esp_timer_start_once(
            s_idle_timer, (uint64_t)S3_DEBUG_AP_IDLE_TIMEOUT_MS * 1000u);
        if (rc != ESP_OK) {
            ESP_LOGE(TAG, "debug AP idle timer start failed: %s",
                     esp_err_to_name(rc));
            stop_ap();
            set_status(S3_DEBUG_AP_STATUS_ERROR);
            return rc;
        }
    }
    ESP_LOGI(TAG, "S3 debug AP active: SSID=%s URL=http://%s",
             S3_DEBUG_AP_SSID, S3_DEBUG_AP_IP);
    set_status(S3_DEBUG_AP_STATUS_ON);
    return ESP_OK;
}

static void s3_debug_ap_worker(void *arg)
{
    (void)arg;
    for (;;) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        const bool desired = s_ap_desired;
        const bool active = s_ap_active;
        if (desired == active) {
            s_ap_worker_running = false;
            xSemaphoreGive(s_lock);
            break;
        }
        xSemaphoreGive(s_lock);

        const esp_err_t rc = s3_debug_ap_apply(desired);

        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (desired && rc != ESP_OK) {
            /* Latch the failure instead of leaving desired != active, which
             * sent this loop straight back into another Wi-Fi and httpd
             * allocation attempt with no delay, forever. */
            s_ap_active = false;
            s_ap_desired = false;
            s_ap_start_failed_latched = true;
            s_ap_worker_running = false;
            xSemaphoreGive(s_lock);
            break;
        }
        s_ap_active = desired;
        xSemaphoreGive(s_lock);
    }
    vTaskDelete(NULL);
}

esp_err_t s3_debug_ap_request(bool enable)
{
    if (!s_lock) {
        /* init not run — fall back to a direct (blocking) apply. */
        const esp_err_t rc = s3_debug_ap_apply(enable);
        s_ap_active = enable ? (rc == ESP_OK) : false;
        return rc;
    }

    bool clear_error = false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (enable && s_ap_start_failed_latched) {
        xSemaphoreGive(s_lock);
        ESP_LOGW(TAG, "debug AP is latched in ERROR; request OFF before retry");
        return ESP_ERR_INVALID_STATE;
    }
    if (!enable && s_ap_start_failed_latched) {
        s_ap_start_failed_latched = false;
        s_ap_desired = false;
        s_ap_active = false;
        clear_error = true;
    } else {
        s_ap_desired = enable;
    }
    const bool spawn = !clear_error && !s_ap_worker_running &&
                       s_ap_desired != s_ap_active;
    if (spawn) s_ap_worker_running = true;
    xSemaphoreGive(s_lock);

    if (clear_error) {
        set_status(S3_DEBUG_AP_STATUS_OFF);
        return ESP_OK;
    }
    if (spawn && xTaskCreate(s3_debug_ap_worker, "s3dbgap", 4096, NULL, 3, NULL) != pdPASS) {
        ESP_LOGE(TAG, "failed to spawn s3 debug AP worker");
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_ap_worker_running = false;
        xSemaphoreGive(s_lock);
        return ESP_ERR_NO_MEM;
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

esp_err_t s3_debug_ap_set_token_callback(s3_debug_ap_token_cb_t cb)
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
