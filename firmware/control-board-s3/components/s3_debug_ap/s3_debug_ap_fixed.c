/*
 * Runtime remediation wrapper for s3_debug_ap.c.
 *
 * The original implementation remains available to the PC tests. Firmware
 * compiles this translation unit instead: selected legacy statics are renamed
 * while the bounded HTTP handler and failure-latched worker below become the
 * production path. This keeps the proven OTA/Wi-Fi implementation intact.
 */
#define events_get_handler       events_get_handler_legacy_blocking
#define start_httpd              start_httpd_legacy_blocking
#define s3_debug_ap_apply        s3_debug_ap_apply_legacy_retrying
#define s3_debug_ap_worker       s3_debug_ap_worker_legacy_retrying
#define s3_debug_ap_request      s3_debug_ap_request_legacy_retrying
#include "s3_debug_ap.c"
#undef events_get_handler
#undef start_httpd
#undef s3_debug_ap_apply
#undef s3_debug_ap_worker
#undef s3_debug_ap_request

#if !defined(S3_DEBUG_AP_PC_TEST) && CONFIG_S3_DEBUG_AP_ENABLED

static bool s_ap_start_failed_latched;

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

    /* One bounded response per request. EventSource reconnects after the server
     * closes this response, so the synchronous ESP-IDF httpd task is never held
     * while /update, firmware status or OTA upload is waiting. */
    if (chunk[0] != '\0' && events_send_sse(req, chunk) != ESP_OK) {
        return ESP_FAIL;
    }
    return httpd_resp_sendstr_chunk(req, NULL);
}

static esp_err_t start_httpd(void)
{
    if (s_httpd) return ESP_OK;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.stack_size = 8192;
    config.lru_purge_enable = true;
    ESP_RETURN_ON_ERROR(httpd_start(&s_httpd, &config), TAG, "httpd_start");

    httpd_uri_t root = {
        .uri = "/", .method = HTTP_GET, .handler = root_get_handler,
    };
    httpd_uri_t events = {
        .uri = "/events", .method = HTTP_GET, .handler = events_get_handler,
    };
    httpd_uri_t update = {
        .uri = "/update", .method = HTTP_GET, .handler = update_get_handler,
    };
    httpd_uri_t firmware = {
        .uri = "/api/firmware", .method = HTTP_GET, .handler = firmware_get_handler,
    };
    httpd_uri_t ota = {
        .uri = "/api/ota/s3", .method = HTTP_POST, .handler = ota_post_handler,
    };
    httpd_uri_t *uris[] = { &root, &events, &update, &firmware, &ota };
    for (size_t i = 0u; i < sizeof(uris) / sizeof(uris[0]); ++i) {
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

    /* start_ap() contains the established netif/Wi-Fi bring-up and temporarily
     * starts the legacy server. Replace it before any client can establish a
     * long-lived /events request. */
    esp_err_t rc = start_ap();
    if (rc == ESP_OK && s_httpd) {
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
            /* ERROR is terminal for this ON edge. Only an explicit OFF request
             * clears the latch; no tight Wi-Fi/httpd allocation retry loop. */
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

#endif
