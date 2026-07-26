#include "web_server.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "audio_engine.h"
#include "media_catalog.h"
#include "ui.h"
#include "ui_library.h"
#include "web_api_helpers.h"
#include "deck_core.h"
#include "control_link.h"
#include "p4_ota.h"
#include "p4_ota_policy.h"
#include "service_log.h"
#include "sd_io_gate.h"
#if CONFIG_AUDIO_RECORDER_ENABLED
#include "audio_recorder.h"
#endif
#include "p4_ota_pull_config.h"
#include "app_settings.h"
#include <stdio.h>
#include "sdkconfig.h"
#if CONFIG_CONTROLLER_PROFILE_MANAGER
#include "controller_profile_manager.h"
#endif
#include <stdlib.h>
#include <string.h>
#include "esp_system.h"
#include "esp_netif.h"
#include "mdns.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "web_server";
static httpd_handle_t s_web_server = NULL;
static bool s_mdns_started;

static void current_ap_ipv4(char out[16])
{
    snprintf(out, 16, "192.168.4.1");
    esp_netif_t *ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    esp_netif_ip_info_t info = {0};
    if (ap && esp_netif_get_ip_info(ap, &info) == ESP_OK) {
        (void)esp_ip4addr_ntoa(&info.ip, out, 16);
    }
}

#if CONFIG_CONTROLLER_PROFILE_MANAGER
static const char *controller_profile_state_name(controller_profile_transfer_state_t state)
{
    switch (state) {
    case CPM_TRANSFER_IDLE:
        return "idle";
    case CPM_TRANSFER_MATCHED:
        return "matched";
    case CPM_TRANSFER_TRANSFERRING:
        return "transferring";
    case CPM_TRANSFER_ACTIVE:
        return "active";
    case CPM_TRANSFER_FAILED:
        return "failed";
    case CPM_TRANSFER_UNSUPPORTED:
        return "unsupported";
    default:
        return "unknown";
    }
}
#endif

static esp_err_t register_uri_or_stop(httpd_handle_t server, const httpd_uri_t *uri)
{
    esp_err_t rc = httpd_register_uri_handler(server, uri);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "URI handler registration failed for %s: %s",
                 uri && uri->uri ? uri->uri : "(null)",
                 esp_err_to_name(rc));
        httpd_stop(server);
        s_web_server = NULL;
    }
    return rc;
}

/* The captive UI is deliberately served from the fixed AP address. Enforce
 * that host on every API request to block DNS rebinding, and require a custom
 * header for mutations so third-party pages cannot trigger deck actions with
 * an image, link or plain HTML form. */
static bool api_request_allowed(httpd_req_t *req, bool mutation)
{
    char host[64] = {0};
    char ap_ipv4[16] = {0};
    current_ap_ipv4(ap_ipv4);
    if (httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) != ESP_OK ||
        !web_api_host_allowed(host, ap_ipv4)) {
        httpd_resp_set_status(req, "403 Forbidden");
        (void)httpd_resp_send(req, "Invalid API host", HTTPD_RESP_USE_STRLEN);
        return false;
    }
    if (mutation) {
        char marker[4] = {0};
        if (httpd_req_get_hdr_value_str(req, "X-DDJ-Control", marker,
                                        sizeof(marker)) != ESP_OK ||
            strcmp(marker, "1") != 0) {
            httpd_resp_set_status(req, "403 Forbidden");
            (void)httpd_resp_send(req, "Missing control request marker",
                                  HTTPD_RESP_USE_STRLEN);
            return false;
        }
    }
    return true;
}

// Simboli za ugrađene datoteke
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");
extern const uint8_t style_css_start[]  asm("_binary_style_css_start");
extern const uint8_t style_css_end[]    asm("_binary_style_css_end");
extern const uint8_t app_js_start[]     asm("_binary_app_js_start");
extern const uint8_t app_js_end[]       asm("_binary_app_js_end");

// Provjera i preusmjeravanje za Captive Portal
static bool redirect_if_needed(httpd_req_t *req)
{
    char host[64] = {0};
    char ap_ipv4[16] = {0};
    current_ap_ipv4(ap_ipv4);
    if (httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) == ESP_OK) {
        if (!web_api_host_allowed(host, ap_ipv4)) {
            ESP_LOGD(TAG, "Redirecting Host '%s' to %s", host,
                     WEB_API_CANONICAL_HOSTNAME);
            httpd_resp_set_status(req, "302 Found");
            httpd_resp_set_hdr(req, "Location",
                               "http://" WEB_API_CANONICAL_HOSTNAME "/index.html");
            httpd_resp_send(req, NULL, 0);
            return true;
        }
    }
    return false;
}

// GET / i /index.html
static esp_err_t index_html_handler(httpd_req_t *req)
{
    ESP_LOGD(TAG, "GET index.html: %s", req->uri);
    if (redirect_if_needed(req)) {
        return ESP_OK;
    }
    size_t size = strlen((const char *)index_html_start);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    return httpd_resp_send(req, (const char *)index_html_start, size);
}

// GET /style.css
static esp_err_t style_css_handler(httpd_req_t *req)
{
    ESP_LOGD(TAG, "GET style.css: %s", req->uri);
    size_t size = strlen((const char *)style_css_start);
    httpd_resp_set_type(req, "text/css");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    return httpd_resp_send(req, (const char *)style_css_start, size);
}

// GET /app.js
static esp_err_t app_js_handler(httpd_req_t *req)
{
    ESP_LOGD(TAG, "GET app.js: %s", req->uri);
    size_t size = strlen((const char *)app_js_start);
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    return httpd_resp_send(req, (const char *)app_js_start, size);
}

static const char *peer_fw_slot_name(uint8_t slot)
{
    switch (slot) {
    case CTRL_FW_SLOT_OTA_0: return "ota_0";
    case CTRL_FW_SLOT_OTA_1: return "ota_1";
    case CTRL_FW_SLOT_FACTORY: return "factory";
    default: return "unknown";
    }
}

static const char *peer_fw_state_name(uint8_t state)
{
    switch (state) {
    case CTRL_FW_STATE_NEW: return "new";
    case CTRL_FW_STATE_PENDING_VERIFY: return "pending_verify";
    case CTRL_FW_STATE_VALID: return "valid";
    case CTRL_FW_STATE_INVALID: return "invalid";
    case CTRL_FW_STATE_ABORTED: return "aborted";
    default: return "unknown";
    }
}

static esp_err_t api_firmware_handler(httpd_req_t *req)
{
    if (!api_request_allowed(req, false)) return ESP_FAIL;
    p4_ota_status_t status;
    p4_ota_get_status(&status);
    ctrl_firmware_report_t s3 = {0};
    bool s3_available = control_link_get_s3_firmware_report(&s3);
    char json[640];
    int len = snprintf(json, sizeof(json),
                       "{\"target\":\"p4\",\"state\":\"%s\","
                       "\"running_slot\":\"%s\",\"running_version\":\"%s\","
                       "\"target_slot\":\"%s\",\"target_version\":\"%s\","
                       "\"expected_size\":%u,\"received_size\":%u,"
                       "\"last_error\":\"%s\","
                       "\"s3\":{\"available\":%s,\"slot\":\"%s\","
                       "\"state\":\"%s\",\"version\":\"%s\"}}",
                       p4_ota_state_name(status.state),
                       status.running_slot, status.running_version,
                       status.target_slot, status.target_version,
                       (unsigned)status.expected_size,
                       (unsigned)status.received_size,
                       status.last_error,
                       s3_available ? "true" : "false",
                       peer_fw_slot_name(s3.slot), peer_fw_state_name(s3.state),
                       s3.version);
    if (len < 0 || (size_t)len >= sizeof(json)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA status overflow");
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, json, (size_t)len);
}

static void ota_restart_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
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


/* ── Pull-OTA service-network configuration ───────────────────────────────── */

/* Wired by app_main; see web_server.h for why this is not a direct call. */
static web_server_probe_start_fn s_probe_start_fn;
static web_server_probe_status_fn s_probe_status_fn;

void web_server_set_probe_hooks(web_server_probe_start_fn start,
                                web_server_probe_status_fn status)
{
    s_probe_start_fn = start;
    s_probe_status_fn = status;
}

/* GET reports the SSID and base URL, and only WHETHER a passphrase is stored.
 * There is deliberately no path that returns the passphrase over the network;
 * to change it you send a new one. */
static esp_err_t api_ota_config_get_handler(httpd_req_t *req)
{
    if (!api_request_allowed(req, false)) return ESP_FAIL;
    char ssid[APP_SETTINGS_OTA_SSID_CAP] = {0};
    char url[APP_SETTINGS_OTA_URL_CAP] = {0};
    app_settings_ota_get_ssid(ssid, sizeof(ssid));
    app_settings_ota_get_url(url, sizeof(url));
    /* The probe result rides along here rather than on its own endpoint: the
     * httpd is capped at 24 URI handlers, and going over takes down the whole
     * web layer including OTA, recoverable only by a wired flash. */
    web_server_probe_status_t probe = {0};
    if (s_probe_status_fn) s_probe_status_fn(&probe);
    static const char *k_probe_names[] = { "idle", "running", "ok", "failed" };
    const char *probe_name = (probe.state >= 0 && probe.state <= 3)
        ? k_probe_names[probe.state] : "idle";

    char json[APP_SETTINGS_OTA_SSID_CAP + APP_SETTINGS_OTA_URL_CAP + 192u];
    int n = snprintf(json, sizeof(json),
                     "{\"ssid\":\"%s\",\"url\":\"%s\",\"has_password\":%s,"
                     "\"probe\":{\"state\":\"%s\",\"detail\":\"%s\","
                     "\"address\":\"%s\"}}",
                     ssid, url,
                     app_settings_ota_has_password() ? "true" : "false",
                     probe_name, probe.detail, probe.address);
    if (n < 0 || (size_t)n >= sizeof(json)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "encode");
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, n);
}

/* POST body: {"ssid":..., "password":..., "url":...} or {"clear":true}.
 *
 * A body rather than query parameters because the passphrase must never reach
 * a URL, where it would be logged. Omitting "password" keeps the stored one,
 * so the SSID or URL can be corrected without retyping it.
 */
static esp_err_t api_ota_config_post_handler(httpd_req_t *req)
{
    if (!api_request_allowed(req, true)) return ESP_FAIL;
    char body[512];
    if (req->content_len <= 0 || (size_t)req->content_len >= sizeof(body)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad body size");
    }
    int got = ota_http_recv(req, (uint8_t *)body, (size_t)req->content_len);
    if (got <= 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Body read failed");
    }
    size_t len = (size_t)got;

    /* A connectivity probe leaves the AP for up to ~25 s. Refuse while audio is
     * playing: the check is here rather than in wifi_link because wifi_link
     * deliberately knows nothing about decks, and because this is the layer
     * that can tell the operator why. */
    bool want_probe = p4_ota_cfg_extract_true(body, len, "probe");
    bool want_check = p4_ota_cfg_extract_true(body, len, "check");
    char install_rel[64] = {0};
    bool want_install = p4_ota_cfg_extract_string(body, len, "install",
                                                  install_rel, sizeof(install_rel));
    if (want_probe || want_check || want_install) {
        memset(body, 0, sizeof(body));
        if (deck_core_get_deck_state(0).playing || deck_core_get_deck_state(1).playing) {
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                       "a deck is playing");
        }
        if (!s_probe_start_fn) {
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                       "probe not wired");
        }
        esp_err_t rc = (esp_err_t)s_probe_start_fn(
            want_install ? 2 : (want_check ? 1 : 0),
            want_install ? install_rel : NULL);
        if (rc == ESP_ERR_INVALID_ARG) {
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                       "no service network or update URL configured");
        }
        if (rc != ESP_OK) {
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                       "probe already running or Wi-Fi is off");
        }
        httpd_resp_set_type(req, "application/json");
        /* 202: the AP is about to disappear for a moment, so the answer has to
         * be sent before the work starts rather than after it. */
        httpd_resp_set_status(req, "202 Accepted");
        return httpd_resp_sendstr(req, "{\"ok\":true,\"probe\":\"started\"}");
    }

    if (p4_ota_cfg_extract_true(body, len, "clear")) {
        app_settings_ota_clear();
        memset(body, 0, sizeof(body));
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"ok\":true,\"cleared\":true}");
    }

    char ssid[APP_SETTINGS_OTA_SSID_CAP] = {0};
    char url[APP_SETTINGS_OTA_URL_CAP] = {0};
    char pass[APP_SETTINGS_OTA_PASS_CAP] = {0};
    bool have_ssid = p4_ota_cfg_extract_string(body, len, "ssid", ssid, sizeof(ssid));
    bool have_url  = p4_ota_cfg_extract_string(body, len, "url", url, sizeof(url));
    bool have_pass = p4_ota_cfg_extract_string(body, len, "password", pass, sizeof(pass));
    /* The body held a passphrase; do not leave it lying in the request buffer
     * for the next handler to reuse. */
    memset(body, 0, sizeof(body));

    const char *reject = NULL;
    if (have_ssid) {
        p4_ota_cfg_result_t r = p4_ota_cfg_check_ssid(ssid);
        if (r != P4_OTA_CFG_OK) reject = p4_ota_cfg_result_name(r);
    }
    if (!reject && have_url) {
        p4_ota_cfg_result_t r = p4_ota_cfg_check_url(url);
        if (r != P4_OTA_CFG_OK) reject = p4_ota_cfg_result_name(r);
    }
    /* An empty password is the legitimate way to say "open network"; anything
     * else must satisfy WPA2 bounds, because a short one would associate never
     * and look like a firmware fault. */
    if (!reject && have_pass && pass[0] != 0) {
        p4_ota_cfg_result_t r = p4_ota_cfg_check_password(pass);
        if (r != P4_OTA_CFG_OK) reject = p4_ota_cfg_result_name(r);
    }
    if (!have_ssid && !have_url && !have_pass) reject = "nothing-to-set";

    if (reject) {
        memset(pass, 0, sizeof(pass));
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, reject);
    }

    esp_err_t rc = app_settings_ota_set(have_ssid ? ssid : NULL,
                                        have_pass ? pass : NULL,
                                        have_url ? url : NULL);
    memset(pass, 0, sizeof(pass));
    if (rc != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   esp_err_to_name(rc));
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

static esp_err_t api_p4_ota_handler(httpd_req_t *req)
{
    if (!api_request_allowed(req, true)) return ESP_FAIL;
    char target[8] = {0};
    if (httpd_req_get_hdr_value_str(req, "X-DDJ-OTA", target, sizeof(target)) != ESP_OK ||
        strcmp(target, "p4") != 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing X-DDJ-OTA: p4");
    }
    if (req->content_len < DDJ_OTA_HEADER_SIZE + P4_OTA_IMAGE_HEADER_SIZE) {
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
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                httpd_resp_set_status(req, "408 Request Timeout");
                return httpd_resp_send(req, "Manifest upload timed out", HTTPD_RESP_USE_STRLEN);
            }
            return ESP_FAIL;
        }
        manifest_received += (size_t)received;
    }

    ddj_ota_manifest_t manifest;
    ddj_ota_manifest_result_t manifest_rc = ddj_ota_manifest_parse(
        manifest_header, sizeof(manifest_header), DDJ_OTA_TARGET_P4,
        P4_OTA_ESP32P4_CHIP_ID, "main-deck-p4", P4_OTA_MAX_IMAGE_SIZE,
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
    while (buffered < P4_OTA_IMAGE_HEADER_SIZE) {
        size_t wanted = remaining < 4096u - buffered ? remaining : 4096u - buffered;
        int received = ota_http_recv(req, buffer + buffered, wanted);
        if (received <= 0) {
            free(buffer);
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                httpd_resp_set_status(req, "408 Request Timeout");
                return httpd_resp_send(req, "Firmware upload timed out", HTTPD_RESP_USE_STRLEN);
            }
            return ESP_FAIL;
        }
        buffered += (size_t)received;
        remaining -= (size_t)received;
    }
    if (!p4_ota_policy_header_valid(buffer, buffered)) {
        free(buffer);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "Not an ESP32-P4 firmware image");
    }

    esp_err_t rc = audio_engine_stop_all();
    if (rc != ESP_OK) {
        free(buffer);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "Cannot stop playback");
    }
    rc = p4_ota_begin(&manifest);
    if (rc != ESP_OK) {
        free(buffer);
        httpd_resp_set_status(req, rc == ESP_ERR_INVALID_STATE ? "409 Conflict" : "400 Bad Request");
        return httpd_resp_send(req, esp_err_to_name(rc), HTTPD_RESP_USE_STRLEN);
    }
    service_log_event(SERVICE_LOG_P4_OTA_STARTED, SERVICE_LOG_INFO,
                      1u, (uint32_t)manifest.image_size, 0u, 0u, 0u, NULL);
    rc = p4_ota_write(buffer, buffered);
    if (rc != ESP_OK) {
        free(buffer);
        p4_ota_abort(esp_err_to_name(rc));
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "Flash write failed");
    }
    while (remaining > 0) {
        size_t wanted = remaining < 4096u ? remaining : 4096u;
        int received = ota_http_recv(req, buffer, wanted);
        if (received <= 0) {
            free(buffer);
            p4_ota_abort("HTTP upload interrupted");
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                httpd_resp_set_status(req, "408 Request Timeout");
                return httpd_resp_send(req, "Firmware upload timed out", HTTPD_RESP_USE_STRLEN);
            }
            return ESP_FAIL;
        }
        rc = p4_ota_write(buffer, (size_t)received);
        if (rc != ESP_OK) {
            free(buffer);
            p4_ota_abort(esp_err_to_name(rc));
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                       "Flash write failed");
        }
        remaining -= (size_t)received;
    }
    free(buffer);

    rc = p4_ota_finish();
    if (rc != ESP_OK) {
        service_log_event(SERVICE_LOG_P4_OTA_FAILED, SERVICE_LOG_ERROR,
                          1u, (uint32_t)rc, 0u, 0u, 0u, "validation");
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "Firmware validation failed");
    }
    service_log_note(SERVICE_LOG_P4_OTA_VERIFIED, SERVICE_LOG_INFO, "reboot pending");

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

#if CONFIG_CONTROLLER_PROFILE_MANAGER
static esp_err_t api_controller_profiles_handler(httpd_req_t *req)
{
    if (!api_request_allowed(req, false)) return ESP_FAIL;
    controller_profile_registry_t *reg = calloc(1, sizeof(*reg));
    char *json = malloc(4096);
    if (!reg || !json) {
        free(reg);
        free(json);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "No memory");
    }
    if (controller_profile_manager_get_registry_snapshot(reg) != ESP_OK) {
        free(reg);
        free(json);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "Profile manager unavailable");
    }
    size_t used = 0;
    int n = snprintf(json, 4096, "{\"profiles\":[");
    if (n < 0 || n >= 4096) {
        free(reg);
        free(json);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "Profile list overflow");
    }
    used = (size_t)n;
    for (uint8_t i = 0; i < reg->count; i++) {
        const controller_profile_meta_t *m = &reg->profiles[i];
        n = snprintf(json + used, 4096 - used,
                     "%s{\"id\":\"%s\",\"valid\":%s,\"vid\":\"0x%04X\","
                     "\"pid\":\"0x%04X\",\"size\":%u}",
                     i == 0 ? "" : ",", m->id,
                     m->valid ? "true" : "false", m->vid, m->pid,
                     (unsigned)m->size);
        if (n < 0 || (size_t)n >= 4096 - used) {
            free(reg);
            free(json);
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                       "Profile list overflow");
        }
        used += (size_t)n;
    }
    n = snprintf(json + used, 4096 - used, "],\"count\":%u}",
                 (unsigned)reg->count);
    if (n < 0 || (size_t)n >= 4096 - used) {
        free(reg);
        free(json);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "Profile list overflow");
    }
    used += (size_t)n;
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t rc = httpd_resp_send(req, json, used);
    free(reg);
    free(json);
    return rc;
}

static esp_err_t api_controller_profile_upload_handler(httpd_req_t *req)
{
    if (!api_request_allowed(req, true)) return ESP_FAIL;

    /* A profile install is a multi-file SD swap; refuse it while the recorder
     * owns the card rather than letting it back-pressure the writer. */
    if (!sd_io_gate_admit(SD_IO_CLASS_PROFILE_UPLOAD, sd_io_gate_recorder_active())) {
        httpd_resp_set_status(req, "409 Conflict");
        return httpd_resp_send(req, "Recording in progress - stop it to install a profile",
                               HTTPD_RESP_USE_STRLEN);
    }

    char id[CPM_ID_MAX] = {0};
    if (httpd_req_get_hdr_value_str(req, "X-DDJ-Profile-ID", id,
                                    sizeof(id)) != ESP_OK ||
        !controller_profile_id_valid(id)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "Invalid or missing X-DDJ-Profile-ID");
    }
    if (!web_api_profile_content_length_valid((size_t)req->content_len)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "Profile size must be 32..16384 bytes");
    }

    char overwrite_header[4] = {0};
    const char *overwrite_value = NULL;
    size_t overwrite_len = httpd_req_get_hdr_value_len(
        req, "X-DDJ-Profile-Overwrite");
    if (overwrite_len > 0) {
        if (overwrite_len >= sizeof(overwrite_header) ||
            httpd_req_get_hdr_value_str(req, "X-DDJ-Profile-Overwrite",
                                        overwrite_header,
                                        sizeof(overwrite_header)) != ESP_OK) {
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                       "Invalid X-DDJ-Profile-Overwrite");
        }
        overwrite_value = overwrite_header;
    }
    bool overwrite = false;
    if (!web_api_profile_overwrite_parse(overwrite_value, &overwrite)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "Overwrite must be 0 or 1");
    }

    size_t expected = (size_t)req->content_len;
    uint8_t *blob = malloc(expected);
    if (!blob) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "No memory");
    }
    size_t received_total = 0;
    while (received_total < expected) {
        int received = ota_http_recv(req, blob + received_total,
                                     expected - received_total);
        if (received <= 0) {
            free(blob);
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                httpd_resp_set_status(req, "408 Request Timeout");
                return httpd_resp_send(req, "Profile upload timed out",
                                       HTTPD_RESP_USE_STRLEN);
            }
            return ESP_FAIL;
        }
        received_total += (size_t)received;
    }

    controller_profile_meta_t meta = {0};
    esp_err_t rc = controller_profile_manager_install_profile(
        id, blob, received_total, overwrite, &meta);
    free(blob);
    if (rc == ESP_ERR_INVALID_ARG) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "Invalid S3CP profile or CRC");
    }
    if (rc == ESP_ERR_INVALID_STATE) {
        httpd_resp_set_status(req, "409 Conflict");
        return httpd_resp_send(req,
                               "Profile exists or manager is busy",
                               HTTPD_RESP_USE_STRLEN);
    }
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "profile '%s' install failed: %s", id,
                 esp_err_to_name(rc));
        service_log_event(SERVICE_LOG_PROFILE_UPLOAD_FAILED, SERVICE_LOG_WARN,
                          1u, (uint32_t)rc, 0u, 0u, 0u, id);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "SD profile install failed");
    }
    service_log_event(SERVICE_LOG_PROFILE_UPLOAD_DONE, SERVICE_LOG_INFO,
                      1u, (uint32_t)received_total, 0u, 0u, 0u, id);

    char json[256];
    int n = snprintf(json, sizeof(json),
                     "{\"ok\":true,\"id\":\"%s\",\"size\":%u,"
                     "\"vid\":\"0x%04X\",\"pid\":\"0x%04X\"}",
                     meta.id, (unsigned)meta.size, meta.vid, meta.pid);
    if (n < 0 || (size_t)n >= sizeof(json)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "Profile response overflow");
    }
    httpd_resp_set_status(req, overwrite ? "200 OK" : "201 Created");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, json, (size_t)n);
}
#endif

// GET /api/status
#if CONFIG_AUDIO_RECORDER_ENABLED
/* ── Master-output recorder API ────────────────────────────────────────────── */

static const char *recording_state_name(audio_recorder_state_t s)
{
    switch (s) {
    case AUDIO_RECORDER_STARTING:  return "STARTING";
    case AUDIO_RECORDER_RECORDING: return "RECORDING";
    case AUDIO_RECORDER_STOPPING:  return "STOPPING";
    case AUDIO_RECORDER_ERROR:     return "ERROR";
    case AUDIO_RECORDER_STOPPED:
    default:                       return "STOPPED";
    }
}

static esp_err_t recording_send_status(httpd_req_t *req)
{
    audio_recorder_status_t st;
    if (audio_recorder_get_status(&st) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "Recorder status unavailable");
    }
    /* Sized with headroom: the fully-populated object with six-digit counters
     * runs a little over 400 bytes, and 320 silently turned every request into
     * a 500 the moment the gate/fwrite split was added. */
    char json[512];
    int n = snprintf(json, sizeof(json),
                     "{\"state\":\"%s\",\"sample_rate\":%u,"
                     "\"ring_used\":%u,\"ring_capacity\":%u,\"ring_high_water\":%u,"
                     "\"dropped_blocks\":%u,\"dropped_frames\":%llu,"
                     "\"bytes_written\":%llu,\"frames_written\":%llu,"
                     "\"push_count\":%u,\"push_max_us\":%u,\"push_over_100us\":%u,"
                     "\"write_max_us\":%u,\"writes_over_100ms\":%u,"
                     "\"gate_wait_max_us\":%u,\"fwrite_max_us\":%u,"
                     "\"last_error\":%d}",
                     recording_state_name(st.state), (unsigned)st.sample_rate,
                     (unsigned)st.ring_used, (unsigned)st.ring_capacity,
                     (unsigned)st.ring_high_water, (unsigned)st.dropped_blocks,
                     (unsigned long long)st.dropped_frames,
                     (unsigned long long)st.bytes_written,
                     (unsigned long long)st.frames_written,
                     (unsigned)st.push_count, (unsigned)st.push_max_us,
                     (unsigned)st.push_over_100us,
                     (unsigned)st.write_max_us, (unsigned)st.writes_over_100ms,
                     (unsigned)st.gate_wait_max_us, (unsigned)st.fwrite_max_us,
                     (int)st.last_error);
    if (n < 0 || (size_t)n >= sizeof(json)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "Recorder response overflow");
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, n);
}

// GET /api/recording — recorder status snapshot.
static esp_err_t api_recording_status_handler(httpd_req_t *req)
{
    if (!api_request_allowed(req, false)) return ESP_FAIL;
    return recording_send_status(req);
}

// POST /api/recording/start — begin recording at the live MAIN output rate.
static esp_err_t api_recording_start_handler(httpd_req_t *req)
{
    if (!api_request_allowed(req, true)) return ESP_FAIL;

    uint32_t rate = audio_engine_get_output_sample_rate();
    if (rate == 0u) {
        httpd_resp_set_status(req, "409 Conflict");
        return httpd_resp_send(req,
                               "No MAIN output rate yet - load and play a track first",
                               HTTPD_RESP_USE_STRLEN);
    }
    /* Start the output-block phase maxima from zero so the numbers describe
     * this recording window rather than whatever happened since boot. */
    audio_engine_reset_output_phase_stats();
    esp_err_t rc = audio_recorder_start(rate);
    if (rc != ESP_OK) {
        httpd_resp_set_status(req, rc == ESP_ERR_INVALID_STATE ? "409 Conflict"
                                                               : "500 Internal Server Error");
        return httpd_resp_send(req, esp_err_to_name(rc), HTTPD_RESP_USE_STRLEN);
    }
    return recording_send_status(req);
}

// POST /api/recording/stop — finalize the current segment.
static esp_err_t api_recording_stop_handler(httpd_req_t *req)
{
    if (!api_request_allowed(req, true)) return ESP_FAIL;
    esp_err_t rc = audio_recorder_stop();
    if (rc != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   esp_err_to_name(rc));
    }
    return recording_send_status(req);
}
#endif  /* CONFIG_AUDIO_RECORDER_ENABLED */

// GET /api/diagnostic-log — stream the active microSD service journal.
static esp_err_t api_diagnostic_log_handler(httpd_req_t *req)
{
    if (!api_request_allowed(req, false)) return ESP_FAIL;

    /* Streaming the whole journal is heavy optional admin work; keep it off the
     * card while the recorder is writing. */
    if (!sd_io_gate_admit(SD_IO_CLASS_LOG_DOWNLOAD, sd_io_gate_recorder_active())) {
        httpd_resp_set_status(req, "409 Conflict");
        return httpd_resp_send(req, "Recording in progress - stop it to download the log",
                               HTTPD_RESP_USE_STRLEN);
    }

    service_log_sync();   /* flush pending records before the snapshot */
    httpd_resp_set_type(req, "text/plain; charset=utf-8");

    sd_io_gate_begin();
    FILE *fp = fopen("/sd/logs/system.log", "rb");
    sd_io_gate_end();
    if (!fp) {
        return httpd_resp_send(req, "service log unavailable\n",
                               HTTPD_RESP_USE_STRLEN);
    }

    char buf[512];
    for (;;) {
        sd_io_gate_begin();
        size_t n = fread(buf, 1, sizeof(buf), fp);
        sd_io_gate_end();
        if (n == 0) {
            break;
        }
        if (httpd_resp_send_chunk(req, buf, (ssize_t)n) != ESP_OK) {
            fclose(fp);
            return ESP_FAIL;
        }
    }
    fclose(fp);
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t api_status_handler(httpd_req_t *req)
{
    if (!api_request_allowed(req, false)) return ESP_FAIL;
    ESP_LOGD(TAG, "GET /api/status: %s", req->uri);
    audio_engine_deck_status_t deck1 = {0};
    audio_engine_deck_status_t deck2 = {0};
    audio_engine_mixer_snapshot_t mixer = {0};
    audio_engine_diagnostics_snapshot_t diagnostics = {0};

    audio_engine_deck_get_status(0, &deck1);
    audio_engine_deck_get_status(1, &deck2);
    audio_engine_get_mixer_snapshot(&mixer);
    audio_engine_get_diagnostics_snapshot(&diagnostics);

    char title1[64] = {0};
    char artist1[64] = {0};
    char title2[64] = {0};
    char artist2[64] = {0};
    uint16_t bpm1_val = 0;
    uint16_t bpm2_val = 0;
    uint32_t duration1_ms = 0;
    uint32_t duration2_ms = 0;

    ui_get_deck_track_info(0, title1, sizeof(title1), artist1, sizeof(artist1), &bpm1_val, &duration1_ms);
    ui_get_deck_track_info(1, title2, sizeof(title2), artist2, sizeof(artist2), &bpm2_val, &duration2_ms);

    char title1_esc[128] = {0};
    char artist1_esc[128] = {0};
    char title2_esc[128] = {0};
    char artist2_esc[128] = {0};
    web_api_json_escape(title1, title1_esc, sizeof(title1_esc));
    web_api_json_escape(artist1, artist1_esc, sizeof(artist1_esc));
    web_api_json_escape(title2, title2_esc, sizeof(title2_esc));
    web_api_json_escape(artist2, artist2_esc, sizeof(artist2_esc));

    const char *state_text1 = "IDLE";
    if (deck1.state == AE_LOADING) state_text1 = "LOADING";
    else if (deck1.state == AE_READY) state_text1 = "READY";
    else if (deck1.state == AE_PLAYING) state_text1 = "PLAYING";
    else if (deck1.state == AE_ERROR) state_text1 = "ERROR";

    const char *state_text2 = "IDLE";
    if (deck2.state == AE_LOADING) state_text2 = "LOADING";
    else if (deck2.state == AE_READY) state_text2 = "READY";
    else if (deck2.state == AE_PLAYING) state_text2 = "PLAYING";
    else if (deck2.state == AE_ERROR) state_text2 = "ERROR";

    deck_state_t state1 = deck_core_get_deck_state(0);
    deck_state_t state2 = deck_core_get_deck_state(1);
    deck_core_beat_fx_state_t beat_fx = deck_core_get_beat_fx_state();

    float p1 = deck_core_pitch_percent(&state1);
    float p2 = deck_core_pitch_percent(&state2);

    uint32_t current_bpm1 = bpm1_val * (1.0f + p1 / 100.0f);
    uint32_t current_bpm2 = bpm2_val * (1.0f + p2 / 100.0f);
    char beat_fx_json[128] = {0};
    web_api_format_beat_fx_json(beat_fx_json,
                                sizeof(beat_fx_json),
                                (int)beat_fx.effect,
                                (int)beat_fx.beat,
                                (int)beat_fx.target,
                                (unsigned)beat_fx.depth,
                                beat_fx.enabled);
    char beat_fx_echo_diag_json[224] = {0};
    web_api_format_beat_fx_echo_diag_json(beat_fx_echo_diag_json,
                                          sizeof(beat_fx_echo_diag_json),
                                          diagnostics.beat_fx_echo_allocated[0],
                                          diagnostics.beat_fx_echo_allocated[1],
                                          diagnostics.beat_fx_echo_enabled[0],
                                          diagnostics.beat_fx_echo_enabled[1],
                                          diagnostics.beat_fx_echo_mode[0] == AUDIO_DELAY_FX_MODE_DELAY,
                                          diagnostics.beat_fx_echo_mode[1] == AUDIO_DELAY_FX_MODE_DELAY,
                                          (unsigned)diagnostics.beat_fx_echo_delay_ms[0],
                                          (unsigned)diagnostics.beat_fx_echo_delay_ms[1]);

    char controller_json[256] = {0};
#if CONFIG_CONTROLLER_PROFILE_MANAGER
    {
        controller_profile_registry_t *reg = calloc(1, sizeof(*reg));
        if (!reg) {
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                       "No memory for profile status");
        }
        (void)controller_profile_manager_get_registry_snapshot(reg);
        char product_esc[2 * CPM_PRODUCT_MAX + 8] = {0};
        web_api_json_escape(reg->connected_product, product_esc, sizeof(product_esc));
        const char *active = (reg->active_index >= 0 &&
                              reg->active_index < (int)reg->count)
            ? reg->profiles[reg->active_index].id : "";
        char active_esc[2 * CPM_ID_MAX + 8] = {0};
        web_api_json_escape(active, active_esc, sizeof(active_esc));
        char state_esc[24] = {0};
        web_api_json_escape(controller_profile_state_name(reg->transfer_state),
                            state_esc, sizeof(state_esc));
        web_api_format_controller_json(controller_json, sizeof(controller_json),
                                       reg->controller_present,
                                       reg->connected_vid, reg->connected_pid,
                                       product_esc,
                                       (reg->connected_caps & CTRL_DESC_CAP_MIDI_IN) != 0,
                                       (reg->connected_caps & CTRL_DESC_CAP_MIDI_OUT) != 0,
                                       (reg->connected_caps & CTRL_DESC_CAP_USB_AUDIO) != 0,
                                       active_esc, state_esc, reg->count);
        free(reg);
    }
#else
    web_api_format_controller_json(controller_json, sizeof(controller_json),
                                   false, 0, 0, "", false, false, false, "", "idle", 0);
#endif

    char *json = NULL;
    int json_len = web_api_alloc_printf(
        &json,
        "{"
        "\"deck1\":{"
        "\"title\":\"%s\","
        "\"artist\":\"%s\","
        "\"bpm\":%u,"
        "\"pitch_percent\":%.2f,"
        "\"raw_pitch\":%d,"
        "\"position_ms\":%u,"
        "\"duration_ms\":%u,"
        "\"playing\":%s,"
        "\"state_text\":\"%s\""
        "},"
        "\"deck2\":{"
        "\"title\":\"%s\","
        "\"artist\":\"%s\","
        "\"bpm\":%u,"
        "\"pitch_percent\":%.2f,"
        "\"raw_pitch\":%d,"
        "\"position_ms\":%u,"
        "\"duration_ms\":%u,"
        "\"playing\":%s,"
        "\"state_text\":\"%s\""
        "},"
        "\"mixer\":{"
        "\"volume1\":%u,"
        "\"volume2\":%u,"
        "\"crossfader\":%u,"
        "\"master_volume\":%u,"
        "\"headphone_mix\":%u,"
        "\"pregain1\":%u,"
        "\"pregain2\":%u,"
        "\"pregain_gain1\":%.3f,"
        "\"pregain_gain2\":%.3f,"
        "\"eq1_low\":%u,"
        "\"eq1_mid\":%u,"
        "\"eq1_high\":%u,"
        "\"eq2_low\":%u,"
        "\"eq2_mid\":%u,"
        "\"eq2_high\":%u,"
        "\"filter1\":%u,"
        "\"filter2\":%u,"
        "\"smart_cfx\":%s,"
        "\"smart_fader\":%s,"
        "\"pfl1\":%s,"
        "\"pfl2\":%s"
        "},"
        "%s,"
        "%s,"
        "\"diagnostics\":{"
        "\"output_codec_open\":%s,"
        "\"output_sample_rate\":%u,"
        "\"output_late_count\":%u,"
        "\"output_late_max_us\":%u,"
        "\"output_late_threshold_us\":%u,"
        "\"pcm_underrun1\":%u,"
        "\"pcm_underrun2\":%u,"
        "\"startup_waiting1\":%s,"
        "\"startup_waiting2\":%s,"
        "\"startup_wait_count1\":%u,"
        "\"startup_wait_count2\":%u,"
        "\"startup_prebuffer_frames\":%u,"
        "\"loop_trim_wraps1\":%u,"
        "\"loop_trim_dropped_max1\":%u,"
        "\"loop_trim_dropped_total1\":%u,"
        "\"loop_trim_clamped_total1\":%u,"
        "\"phase_head_us\":%u,"
        "\"phase_mix_us\":%u,"
        "\"phase_push_us\":%u,"
        "\"phase_monitor_us\":%u,"
        "\"phase_main_us\":%u,"
        "\"phase_codec_us\":%u,"
        "\"phase_book_us\":%u,"
        "\"ring_capacity\":%u,"
        "\"ring_used1\":%u,"
        "\"ring_used2\":%u,"
        "\"deck_sample_rate1\":%u,"
        "\"deck_sample_rate2\":%u,"
        "\"deck_channels1\":%u,"
        "\"deck_channels2\":%u,"
        "\"deck_file_bytes1\":%u,"
        "\"deck_file_bytes2\":%u,"
        "\"deck_load_progress1\":%u,"
        "\"deck_load_progress2\":%u,"
        "\"deck_active1\":%s,"
        "\"deck_active2\":%s,"
        "\"limiter_samples\":%u,"
        "\"limiter_positive\":%u,"
        "\"limiter_negative\":%u,"
        "\"limiter_peak\":%d,"
        "\"usb_headphones\":{\"submitted_blocks\":%u,\"dropped_blocks\":%u,\"submitted_frames\":%u},"
        "%s,"
        "\"heap_free\":%u,"
        "\"internal_free\":%u,"
        "\"psram_free\":%u"
        "}"
        "}",
        title1_esc, artist1_esc, (unsigned)current_bpm1, p1, state1.pitch, (unsigned)state1.position_ms, (unsigned)duration1_ms, state1.playing ? "true" : "false", state_text1,
        title2_esc, artist2_esc, (unsigned)current_bpm2, p2, state2.pitch, (unsigned)state2.position_ms, (unsigned)duration2_ms, state2.playing ? "true" : "false", state_text2,
        mixer.channel_volume[0], mixer.channel_volume[1], mixer.crossfader,
        mixer.master_volume,
        mixer.headphone_mix,
        mixer.pregain[0], mixer.pregain[1],
        (double)mixer.pregain_gain[0], (double)mixer.pregain_gain[1],
        mixer.eq[0][AUDIO_EQ_BAND_LOW], mixer.eq[0][AUDIO_EQ_BAND_MID], mixer.eq[0][AUDIO_EQ_BAND_HIGH],
        mixer.eq[1][AUDIO_EQ_BAND_LOW], mixer.eq[1][AUDIO_EQ_BAND_MID], mixer.eq[1][AUDIO_EQ_BAND_HIGH],
        mixer.filter[0], mixer.filter[1],
        mixer.smart_cfx_enabled ? "true" : "false",
        mixer.smart_fader_enabled ? "true" : "false",
        mixer.pfl_enabled[0] ? "true" : "false", mixer.pfl_enabled[1] ? "true" : "false",
        beat_fx_json,
        controller_json,
        diagnostics.output_codec_open ? "true" : "false",
        (unsigned)diagnostics.output_sample_rate,
        (unsigned)diagnostics.output_late_count,
        (unsigned)diagnostics.output_late_max_us,
        (unsigned)diagnostics.output_late_threshold_us,
        (unsigned)diagnostics.pcm_underrun_count[0],
        (unsigned)diagnostics.pcm_underrun_count[1],
        diagnostics.startup_waiting[0] ? "true" : "false",
        diagnostics.startup_waiting[1] ? "true" : "false",
        (unsigned)diagnostics.startup_wait_count[0],
        (unsigned)diagnostics.startup_wait_count[1],
        (unsigned)diagnostics.startup_prebuffer_frames,
        (unsigned)diagnostics.loop_trim_wraps[0],
        (unsigned)diagnostics.loop_trim_dropped_max[0],
        (unsigned)diagnostics.loop_trim_dropped_total[0],
        (unsigned)diagnostics.loop_trim_clamped_total[0],
        (unsigned)diagnostics.phase_head_max_us,
        (unsigned)diagnostics.phase_mix_max_us,
        (unsigned)diagnostics.phase_push_max_us,
        (unsigned)diagnostics.phase_monitor_max_us,
        (unsigned)diagnostics.phase_main_max_us,
        (unsigned)diagnostics.phase_codec_max_us,
        (unsigned)diagnostics.phase_book_max_us,
        (unsigned)diagnostics.ring_capacity,
        (unsigned)diagnostics.ring_used[0],
        (unsigned)diagnostics.ring_used[1],
        (unsigned)diagnostics.deck_sample_rate[0],
        (unsigned)diagnostics.deck_sample_rate[1],
        (unsigned)diagnostics.deck_channels[0],
        (unsigned)diagnostics.deck_channels[1],
        (unsigned)diagnostics.deck_file_bytes[0],
        (unsigned)diagnostics.deck_file_bytes[1],
        (unsigned)diagnostics.deck_load_progress[0],
        (unsigned)diagnostics.deck_load_progress[1],
        diagnostics.deck_active[0] ? "true" : "false",
        diagnostics.deck_active[1] ? "true" : "false",
        (unsigned)diagnostics.limiter.limited_samples,
        (unsigned)diagnostics.limiter.positive_overloads,
        (unsigned)diagnostics.limiter.negative_overloads,
        (int)diagnostics.limiter.peak_input_abs,
        (unsigned)diagnostics.usb_headphone_submitted_blocks,
        (unsigned)diagnostics.usb_headphone_dropped_blocks,
        (unsigned)diagnostics.usb_headphone_submitted_frames,
        beat_fx_echo_diag_json,
        (unsigned)diagnostics.heap_free,
        (unsigned)diagnostics.internal_free,
        (unsigned)diagnostics.psram_free);
    if (!json || json_len < 0) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No memory");
        return ESP_ERR_NO_MEM;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, (size_t)json_len);
    free(json);
    return ESP_OK;
}

// GET /api/library
static esp_err_t api_library_handler(httpd_req_t *req)
{
    if (!api_request_allowed(req, false)) return ESP_FAIL;
    ESP_LOGD(TAG, "GET /api/library: %s", req->uri);
    int count = media_catalog_count();

    // Alociramo manji buffer u RAM-u za chunkove
    size_t chunk_sz = 4096;
    char *chunk = malloc(chunk_sz);
    if (!chunk) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "No memory");
    }

    httpd_resp_set_type(req, "application/json");

    // Pošalji početak JSON-a
    const char *header = "{\"tracks\":[";
    esp_err_t send_rc = httpd_resp_send_chunk(req, header, strlen(header));
    if (send_rc != ESP_OK) {
        free(chunk);
        return send_rc;
    }

    int chunk_len = 0;
    bool first = true;

    for (int i = 0; i < count; i++) {
        media_catalog_row_t row;
        if (media_catalog_get_row(i, &row) == ESP_OK) {
            char title_esc[256];
            char artist_esc[256];
            char item[768];
            web_api_json_escape(row.title, title_esc, sizeof(title_esc));
            web_api_json_escape(row.artist, artist_esc, sizeof(artist_esc));
            int item_len = snprintf(item, sizeof(item),
                                    "%s{\"index\":%d,\"title\":\"%s\",\"artist\":\"%s\",\"bpm\":%u,\"duration_ms\":%u}",
                                    first ? "" : ",",
                                    i, title_esc, artist_esc, row.bpm, (unsigned)row.duration_ms);
            if (item_len < 0 || (size_t)item_len >= sizeof(item)) {
                ESP_LOGW(TAG, "Skipping oversized library JSON row index=%d", i);
                continue;
            }

            // Ako bi dodavanje ovog stavka premašilo sigurnosnu granicu chunka, pošalji trenutni chunk
            if (chunk_len + item_len >= chunk_sz - 10) {
                send_rc = httpd_resp_send_chunk(req, chunk, chunk_len);
                chunk_len = 0;
                // Klijent je prekinuo vezu: prestani slati ostatak liste.
                if (send_rc != ESP_OK) {
                    break;
                }
            }

            memcpy(chunk + chunk_len, item, item_len);
            chunk_len += item_len;
            first = false;
        }
    }

    // Pošalji preostali dio chunka
    if (send_rc == ESP_OK && chunk_len > 0) {
        send_rc = httpd_resp_send_chunk(req, chunk, chunk_len);
    }

    free(chunk);

    if (send_rc != ESP_OK) {
        // Veza je pukla; abortaj prijenos (ne šalji footer ni terminating chunk).
        ESP_LOGW(TAG, "library JSON send aborted: %s", esp_err_to_name(send_rc));
        return send_rc;
    }

    // Pošalji kraj JSON-a
    const char *footer = "]}";
    send_rc = httpd_resp_send_chunk(req, footer, strlen(footer));
    if (send_rc != ESP_OK) {
        return send_rc;
    }

    // Pošalji prazan chunk za označavanje kraja prijenosa
    return httpd_resp_send_chunk(req, NULL, 0);
}

// POST /api/control (query parameters, protected by X-DDJ-Control)
static bool api_parse_deck(const char *value, uint8_t *out_deck)
{
    int32_t parsed = 0;
    if (!out_deck || !web_api_parse_int32(value, 1, 2, &parsed)) {
        return false;
    }
    *out_deck = parsed == 2 ? CTRL_DECK_2 : CTRL_DECK_1;
    return true;
}

static esp_err_t api_control_handler(httpd_req_t *req)
{
    if (!api_request_allowed(req, true)) return ESP_FAIL;
    char query[128] = {0};
    char deck_str[16] = {0};
    char action[32] = {0};
    char value_str[32] = {0};

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "action", action, sizeof(action)) != ESP_OK ||
        action[0] == '\0') {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "Missing or oversized action");
    }
    bool has_deck = httpd_query_key_value(query, "deck", deck_str,
                                           sizeof(deck_str)) == ESP_OK;
    bool has_value = httpd_query_key_value(query, "value", value_str,
                                            sizeof(value_str)) == ESP_OK;

    uint8_t deck = CTRL_DECK_NONE;
    bool deck_required = strcmp(action, "crossfader") != 0;
    if ((deck_required && !has_deck) ||
        (has_deck && !api_parse_deck(deck_str, &deck))) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "Deck must be 1 or 2");
    }

    int32_t value = 0;
    bool control_value_required = strcmp(action, "volume") == 0 ||
                                  strcmp(action, "crossfader") == 0 ||
                                  strcmp(action, "pitch") == 0;
    bool seek_value_required = strcmp(action, "seek") == 0;
    if (control_value_required &&
        (!has_value || !web_api_parse_int32(value_str, 0,
                                             AUDIO_MIXER_CONTROL_MAX, &value))) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "Control value must be 0..16383");
    }
    if (seek_value_required &&
        (!has_value || !web_api_parse_int32(value_str, 0, INT32_MAX, &value))) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "Seek value must be a non-negative integer");
    }

    ESP_LOGI(TAG, "Control action: deck=%d, action=%s, value=%ld", deck,
             action, (long)value);

    esp_err_t queue_rc = ESP_OK;
    if (strcmp(action, "play_pause") == 0) {
        ctrl_event_t ev = {
            .type  = CTRL_EV_BUTTON,
            .id    = (deck == CTRL_DECK_2) ? CTRL_ID_DECK2_PLAY : CTRL_ID_DECK1_PLAY,
            .deck  = deck,
            .value = 1,
            .seq   = 0
        };
        queue_rc = deck_core_queue_event(&ev);
    } else if (strcmp(action, "cue") == 0) {
        ctrl_event_t ev = {
            .type  = CTRL_EV_BUTTON,
            .id    = (deck == CTRL_DECK_2) ? CTRL_ID_DECK2_CUE : CTRL_ID_DECK1_CUE,
            .deck  = deck,
            .value = 1,
            .seq   = 0
        };
        queue_rc = deck_core_queue_event(&ev);
    } else if (strcmp(action, "pfl") == 0) {
        ctrl_event_t ev = {
            .type  = CTRL_EV_BUTTON,
            .id    = (deck == CTRL_DECK_2) ? CTRL_ID_DECK2_PFL : CTRL_ID_DECK1_PFL,
            .deck  = deck,
            .value = 1,
            .seq   = 0
        };
        queue_rc = deck_core_queue_event(&ev);
    } else if (strcmp(action, "volume") == 0) {
        ctrl_event_t ev = {
            .type  = CTRL_EV_BUTTON,
            .id    = (deck == CTRL_DECK_2) ? CTRL_ID_CH2_VOLUME : CTRL_ID_CH1_VOLUME,
            .deck  = deck,
            .value = (int16_t)value,
            .seq   = 0
        };
        queue_rc = deck_core_queue_event(&ev);
    } else if (strcmp(action, "crossfader") == 0) {
        ctrl_event_t ev = {
            .type  = CTRL_EV_BUTTON,
            .id    = CTRL_ID_CROSSFADER,
            .deck  = CTRL_DECK_NONE,
            .value = (int16_t)value,
            .seq   = 0
        };
        queue_rc = deck_core_queue_event(&ev);
    } else if (strcmp(action, "pitch") == 0) {
        ctrl_event_t ev = {
            .type  = CTRL_EV_PITCH,
            .id    = (deck == CTRL_DECK_2) ? CTRL_ID_DECK2_TEMPO : CTRL_ID_DECK1_TEMPO,
            .deck  = deck,
            .value = (int16_t)value,
            .seq   = 0
        };
        queue_rc = deck_core_queue_event(&ev);
    } else if (strcmp(action, "loop_4") == 0) {
        audio_engine_deck_status_t status = {0};
        esp_err_t rc = audio_engine_deck_get_status(deck, &status);
        if (rc != ESP_OK) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid deck");
            return ESP_FAIL;
        }
        uint32_t pos = status.position_ms;
        uint16_t bpm = ui_library_deck_bpm(deck, 120);
        if (bpm == 0) {
            bpm = 120;
        }
        uint32_t beat_len_ms = 60000u / bpm;
        uint32_t loop_len_ms = 4u * beat_len_ms;
        uint32_t loop_end = pos > UINT32_MAX - loop_len_ms
            ? UINT32_MAX : pos + loop_len_ms;
        rc = audio_engine_deck_set_loop(deck, pos, loop_end);
        if (rc != ESP_OK) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Loop failed");
            return ESP_FAIL;
        }
    } else if (strcmp(action, "loop_clear") == 0) {
        esp_err_t rc = audio_engine_deck_clear_loop(deck);
        if (rc != ESP_OK) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Loop clear failed");
            return ESP_FAIL;
        }
    } else if (strcmp(action, "seek") == 0) {
        audio_engine_deck_status_t status = {0};
        if (audio_engine_deck_get_status(deck, &status) != ESP_OK ||
            !status.loaded ||
            status.state == AE_ERROR) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Deck not seekable");
            return ESP_FAIL;
        }
        uint32_t duration_ms = 0;
        ui_get_deck_track_info(deck, NULL, 0, NULL, 0, NULL, &duration_ms);
        uint32_t pos_ms = web_api_clamp_seek_ms(value, duration_ms, duration_ms > 0u);
        esp_err_t rc = audio_engine_deck_seek(deck, pos_ms);
        if (rc != ESP_OK) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Seek failed");
            return ESP_FAIL;
        }
    } else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Unknown action");
        return ESP_FAIL;
    }

    if (queue_rc != ESP_OK) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_send(req, "Control queue busy", HTTPD_RESP_USE_STRLEN);
    }
    httpd_resp_send(req, "OK", 2);
    return ESP_OK;
}

// POST /api/load (query parameters, protected by X-DDJ-Control)
static esp_err_t api_load_handler(httpd_req_t *req)
{
    if (!api_request_allowed(req, true)) return ESP_FAIL;
    char query[64] = {0};
    char index_str[16] = {0};
    char deck_str[16] = {0};

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "index", index_str,
                              sizeof(index_str)) != ESP_OK ||
        httpd_query_key_value(query, "deck", deck_str,
                              sizeof(deck_str)) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "Missing or oversized load parameters");
    }

    int32_t index = 0;
    uint8_t deck = CTRL_DECK_NONE;
    if (!web_api_parse_int32(index_str, 0, INT32_MAX, &index) ||
        !api_parse_deck(deck_str, &deck) ||
        index >= media_catalog_count()) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "Invalid track index or deck");
    }

    ESP_LOGI(TAG, "API Load Request: index=%ld, deck=%d", (long)index, deck);

    esp_err_t rc = ui_library_load_track_index_for_deck((int)index, deck);
    if (rc != ESP_OK) {
        service_log_event(SERVICE_LOG_WEB_LOAD_REQ_FAILED, SERVICE_LOG_WARN,
                          3u, (uint32_t)index, (uint32_t)deck, (uint32_t)rc, 0u, NULL);
        if (rc == ESP_ERR_INVALID_STATE) {
            httpd_resp_set_status(req, "409 Conflict");
            httpd_resp_send(req, "Load busy", HTTPD_RESP_USE_STRLEN);
            return ESP_FAIL;
        }
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Load failed");
        return ESP_FAIL;
    }

    httpd_resp_send(req, "OK", 2);
    return ESP_OK;
}

// Catch-all handler za Captive Portal
static esp_err_t catch_all_handler(httpd_req_t *req)
{
    // Ako klijent traži bilo što, a mi smo u Captive Portal modu, preusmjeri na index.html
    ESP_LOGD(TAG, "Catch-all request: %s", req->uri);
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location",
                       "http://" WEB_API_CANONICAL_HOSTNAME "/index.html");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t web_server_start(void)
{
    if (s_web_server != NULL) {
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 5;
    /* Evict the least-recently-used socket instead of refusing the connection.
     * The controller page holds keep-alive sockets while it polls /api/status,
     * so without this a single busy browser can occupy all five and lock every
     * other client out completely — including the OTA endpoint, from a device
     * that still answers ping and looks perfectly healthy. */
    config.lru_purge_enable = true;
    config.stack_size = 8192;
    config.ctrl_port = 32768; // pomaknuto da ne bude u konfliktu
    config.uri_match_fn = httpd_uri_match_wildcard;
    /* Must stay above the number of register_uri_or_stop() calls below: a single
     * failed registration stops the whole server, so an over-tight limit takes
     * every endpoint (including OTA) down with it. */
    config.max_uri_handlers = 24;
    config.task_priority = 3;
    config.core_id = 0;

    // Pokreni poslužitelj
    ESP_LOGI(TAG, "Pokretanje HTTP poslužitelja na portu %d...", config.server_port);
    esp_err_t rc = httpd_start(&s_web_server, &config);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "Ne mogu pokrenuti HTTP poslužitelj: %s", esp_err_to_name(rc));
        return rc;
    }

    // Registracija URI handlera
    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_html_handler,
        .user_ctx = NULL
    };
    rc = register_uri_or_stop(s_web_server, &index_uri);
    if (rc != ESP_OK) return rc;

    httpd_uri_t index_html_uri = {
        .uri = "/index.html*",
        .method = HTTP_GET,
        .handler = index_html_handler,
        .user_ctx = NULL
    };
    rc = register_uri_or_stop(s_web_server, &index_html_uri);
    if (rc != ESP_OK) return rc;

    httpd_uri_t style_uri = {
        .uri = "/style.css*",
        .method = HTTP_GET,
        .handler = style_css_handler,
        .user_ctx = NULL
    };
    rc = register_uri_or_stop(s_web_server, &style_uri);
    if (rc != ESP_OK) return rc;

    httpd_uri_t js_uri = {
        .uri = "/app.js*",
        .method = HTTP_GET,
        .handler = app_js_handler,
        .user_ctx = NULL
    };
    rc = register_uri_or_stop(s_web_server, &js_uri);
    if (rc != ESP_OK) return rc;

    httpd_uri_t status_uri = {
        .uri = "/api/status*",
        .method = HTTP_GET,
        .handler = api_status_handler,
        .user_ctx = NULL
    };
    rc = register_uri_or_stop(s_web_server, &status_uri);
    if (rc != ESP_OK) return rc;

#if CONFIG_AUDIO_RECORDER_ENABLED
    httpd_uri_t recording_status_uri = {
        .uri = "/api/recording",
        .method = HTTP_GET,
        .handler = api_recording_status_handler,
        .user_ctx = NULL
    };
    rc = register_uri_or_stop(s_web_server, &recording_status_uri);
    if (rc != ESP_OK) return rc;

    httpd_uri_t recording_start_uri = {
        .uri = "/api/recording/start",
        .method = HTTP_POST,
        .handler = api_recording_start_handler,
        .user_ctx = NULL
    };
    rc = register_uri_or_stop(s_web_server, &recording_start_uri);
    if (rc != ESP_OK) return rc;

    httpd_uri_t recording_stop_uri = {
        .uri = "/api/recording/stop",
        .method = HTTP_POST,
        .handler = api_recording_stop_handler,
        .user_ctx = NULL
    };
    rc = register_uri_or_stop(s_web_server, &recording_stop_uri);
    if (rc != ESP_OK) return rc;

#endif  /* CONFIG_AUDIO_RECORDER_ENABLED */

    httpd_uri_t diag_log_uri = {
        .uri = "/api/diagnostic-log",
        .method = HTTP_GET,
        .handler = api_diagnostic_log_handler,
        .user_ctx = NULL
    };
    rc = register_uri_or_stop(s_web_server, &diag_log_uri);
    if (rc != ESP_OK) return rc;

    httpd_uri_t firmware_uri = {
        .uri = "/api/firmware*",
        .method = HTTP_GET,
        .handler = api_firmware_handler,
        .user_ctx = NULL
    };
    rc = register_uri_or_stop(s_web_server, &firmware_uri);
    if (rc != ESP_OK) return rc;

    httpd_uri_t ota_cfg_get_uri = {
        .uri = "/api/ota/config",
        .method = HTTP_GET,
        .handler = api_ota_config_get_handler,
        .user_ctx = NULL
    };
    rc = register_uri_or_stop(s_web_server, &ota_cfg_get_uri);
    if (rc != ESP_OK) return rc;

    httpd_uri_t ota_cfg_post_uri = {
        .uri = "/api/ota/config",
        .method = HTTP_POST,
        .handler = api_ota_config_post_handler,
        .user_ctx = NULL
    };
    rc = register_uri_or_stop(s_web_server, &ota_cfg_post_uri);
    if (rc != ESP_OK) return rc;

    httpd_uri_t ota_p4_uri = {
        .uri = "/api/ota/p4",
        .method = HTTP_POST,
        .handler = api_p4_ota_handler,
        .user_ctx = NULL
    };
    rc = register_uri_or_stop(s_web_server, &ota_p4_uri);
    if (rc != ESP_OK) return rc;

#if CONFIG_CONTROLLER_PROFILE_MANAGER
    httpd_uri_t controller_profiles_uri = {
        .uri = "/api/controller-profiles",
        .method = HTTP_GET,
        .handler = api_controller_profiles_handler,
        .user_ctx = NULL
    };
    rc = register_uri_or_stop(s_web_server, &controller_profiles_uri);
    if (rc != ESP_OK) return rc;

    httpd_uri_t controller_profile_upload_uri = {
        .uri = "/api/controller-profile",
        .method = HTTP_POST,
        .handler = api_controller_profile_upload_handler,
        .user_ctx = NULL
    };
    rc = register_uri_or_stop(s_web_server, &controller_profile_upload_uri);
    if (rc != ESP_OK) return rc;
#endif

    httpd_uri_t library_uri = {
        .uri = "/api/library*",
        .method = HTTP_GET,
        .handler = api_library_handler,
        .user_ctx = NULL
    };
    rc = register_uri_or_stop(s_web_server, &library_uri);
    if (rc != ESP_OK) return rc;

    httpd_uri_t control_uri = {
        .uri = "/api/control*",
        .method = HTTP_POST,
        .handler = api_control_handler,
        .user_ctx = NULL
    };
    rc = register_uri_or_stop(s_web_server, &control_uri);
    if (rc != ESP_OK) return rc;

    httpd_uri_t load_uri = {
        .uri = "/api/load*",
        .method = HTTP_POST,
        .handler = api_load_handler,
        .user_ctx = NULL
    };
    rc = register_uri_or_stop(s_web_server, &load_uri);
    if (rc != ESP_OK) return rc;

    // Registracija catch-all za preusmjeravanje Captive Portala
    httpd_uri_t catch_all_uri = {
        .uri = "*",
        .method = HTTP_GET,
        .handler = catch_all_handler,
        .user_ctx = NULL
    };
    // Zbog poretka pretraživanja registrirat ćemo ga na kraju
    rc = register_uri_or_stop(s_web_server, &catch_all_uri);
    if (rc != ESP_OK) return rc;

    rc = mdns_init();
    if (rc == ESP_OK) {
        rc = mdns_hostname_set("pajoniiir");
    }
    if (rc == ESP_OK) {
        rc = mdns_instance_name_set("Pajoniiir");
    }
    if (rc == ESP_OK) {
        rc = mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    }
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "mDNS startup failed: %s", esp_err_to_name(rc));
        mdns_free();
        httpd_stop(s_web_server);
        s_web_server = NULL;
        return rc;
    }
    s_mdns_started = true;
    ESP_LOGI(TAG, "web UI available at http://%s", WEB_API_CANONICAL_HOSTNAME);

    return ESP_OK;
}

void web_server_stop(void)
{
    if (s_mdns_started) {
        mdns_free();
        s_mdns_started = false;
    }
    if (s_web_server != NULL) {
        httpd_stop(s_web_server);
        s_web_server = NULL;
    }
}
