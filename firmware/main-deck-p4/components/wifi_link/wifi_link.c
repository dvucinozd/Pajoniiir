#include "wifi_link.h"
#include "wifi_link_retry.h"
#include "web_server.h"
#include "service_log.h"
#include "app_settings.h"
#include "esp_heap_caps.h"

#include "esp_check.h"
#include "esp_event.h"
#include "esp_hosted.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
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

/* STA association is asynchronous, so the worker has to wait on events rather
 * than on a return code. GOT_IP is the only success signal that means anything:
 * associating without an address fetches nothing. */
#define STA_BIT_GOT_IP       BIT0
#define STA_BIT_DISCONNECTED BIT1

static EventGroupHandle_t s_sta_events;
static esp_netif_t *s_sta_netif;
static volatile bool s_sta_mode;

static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        if (s_sta_events) xEventGroupSetBits(s_sta_events, STA_BIT_GOT_IP);
        return;
    }
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        /* Reported for a refused association and for a later drop alike. The
         * waiter treats it as failure; a drop after we already have an address
         * is handled by the caller finishing and restoring. */
        if (s_sta_events) xEventGroupSetBits(s_sta_events, STA_BIT_DISCONNECTED);
        return;
    }
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
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_handler_instance_register(
            IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, NULL, NULL));
        if (!s_sta_events) {
            s_sta_events = xEventGroupCreate();
        }
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

/*
 * Teardown split into independently owned steps.
 *
 * wifi_link_stop() still runs all four in the same order and has exactly the
 * behaviour it always had; the split exists because an AP-to-STA transition
 * needs the first two without the last two. Tearing down ESP-Hosted only to
 * recreate the C6 link a moment later is both slow and an unnecessary chance
 * for the transport to come back wrong.
 *
 * Each step is best-effort and idempotent, so a partial bring-up from a failed
 * start still unwinds completely.
 */

/* Captive DNS and the HTTP service. Safe to call when they are not running. */
static void stop_ap_services(void)
{
    dns_server_stop();
    web_server_stop();
}

static void stop_wifi_stack(void)
{
    if (!s_wifi_ready) return;
    esp_wifi_stop();
    esp_wifi_deinit();
    s_wifi_ready = false;
}

static void stop_ap_netif(void)
{
    if (!s_ap_netif) return;
    esp_netif_destroy_default_wifi(s_ap_netif);
    s_ap_netif = NULL;
}

/* Releases the C6 link so it stops drawing RAM and radio. Deliberately the
 * last thing to go and the one an AP/STA switch must NOT do. */
static void stop_hosted_transport(void)
{
    if (!s_hosted_ready) return;
    esp_hosted_deinit();
    s_hosted_ready = false;
}

static void stop_sta_netif(void)
{
    if (!s_sta_netif) return;
    esp_netif_destroy_default_wifi(s_sta_netif);
    s_sta_netif = NULL;
}

/* ── Temporary STA visit ──────────────────────────────────────────────────── */

esp_err_t wifi_link_switch_to_sta(const char *ssid, const char *password,
                                  uint32_t timeout_ms)
{
    if (!ssid || ssid[0] == '\0') return ESP_ERR_INVALID_ARG;
    if (!s_wifi_ready || !s_hosted_ready) return ESP_ERR_INVALID_STATE;
    if (!s_sta_events) return ESP_ERR_INVALID_STATE;

    /* Drop the AP's services and interface but keep esp_wifi and the C6 link
     * up — that separation is the whole reason the teardown was split. */
    stop_ap_services();
    ESP_RETURN_ON_ERROR(esp_wifi_stop(), TAG, "stop before STA");
    stop_ap_netif();
    s_status.active = false;
    s_status.ap_clients = 0;

    s_sta_netif = esp_netif_create_default_wifi_sta();
    if (!s_sta_netif) return ESP_ERR_NO_MEM;

    wifi_config_t cfg = {0};
    copy_wifi_bytes(cfg.sta.ssid, sizeof(cfg.sta.ssid), ssid);
    copy_wifi_bytes(cfg.sta.password, sizeof(cfg.sta.password), password);
    /* An empty password means an open network; anything else was already
     * validated against WPA2 bounds before it reached storage. */
    cfg.sta.threshold.authmode =
        (password && password[0]) ? WIFI_AUTH_WPA_WPA2_PSK : WIFI_AUTH_OPEN;

    xEventGroupClearBits(s_sta_events, STA_BIT_GOT_IP | STA_BIT_DISCONNECTED);

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set STA mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &cfg), TAG, "set STA config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start STA");
    s_sta_mode = true;
    ESP_LOGI(TAG, "joining service network \"%s\"", ssid);
    ESP_RETURN_ON_ERROR(esp_wifi_connect(), TAG, "connect");

    /* Bounded on purpose: a wrong passphrase produces a disconnect, but a
     * network that associates and never serves DHCP produces nothing at all,
     * and the deck must not sit off-AP indefinitely waiting for it. */
    EventBits_t bits = xEventGroupWaitBits(
        s_sta_events, STA_BIT_GOT_IP | STA_BIT_DISCONNECTED,
        pdFALSE, pdFALSE, pdMS_TO_TICKS(timeout_ms));

    if (bits & STA_BIT_GOT_IP) {
        esp_netif_ip_info_t ip = {0};
        if (esp_netif_get_ip_info(s_sta_netif, &ip) == ESP_OK) {
            ESP_LOGI(TAG, "service network address " IPSTR, IP2STR(&ip.ip));
        }
        return ESP_OK;
    }
    if (bits & STA_BIT_DISCONNECTED) {
        ESP_LOGW(TAG, "service network refused the association");
        return ESP_ERR_WIFI_NOT_CONNECT;
    }
    ESP_LOGW(TAG, "service network gave no address within %u ms",
             (unsigned)timeout_ms);
    return ESP_ERR_TIMEOUT;
}

esp_err_t wifi_link_restore_ap(void)
{
    /* Unconditional: this is the path back to being reachable at all, so it
     * runs the same way whether the visit succeeded, failed or never got
     * started. */
    if (s_sta_mode) {
        esp_wifi_disconnect();
        esp_wifi_stop();
        s_sta_mode = false;
    }
    stop_sta_netif();

    esp_err_t rc = start_web_ap();
    if (rc == ESP_OK) rc = web_server_start();
    if (rc == ESP_OK) rc = dns_server_start();

    s_status.last_error = rc;
    if (rc == ESP_OK) {
        s_active = true;
        s_status.active = true;
        ESP_LOGI(TAG, "%s restored", s_status.ssid);
    } else {
        /* Nothing left to fall back to: say so loudly rather than leave a
         * half-configured radio looking healthy. Recovery is a wired flash. */
        ESP_LOGE(TAG, "FAILED to restore %s: %s", s_status.ssid,
                 esp_err_to_name(rc));
        s_active = false;
        s_status.active = false;
    }
    return rc;
}

bool wifi_link_is_sta(void)
{
    return s_sta_mode;
}

/* ── One-shot connectivity probe ──────────────────────────────────────────── */

static wifi_link_probe_status_t s_probe;
static volatile bool s_probe_running;

static void probe_note(wifi_link_probe_state_t state, esp_err_t err,
                       const char *detail)
{
    s_probe.state = state;
    s_probe.last_error = err;
    snprintf(s_probe.detail, sizeof(s_probe.detail), "%s", detail ? detail : "");
}

static void wifi_link_probe_task(void *arg)
{
    (void)arg;
    char ssid[APP_SETTINGS_OTA_SSID_CAP] = {0};
    char pass[APP_SETTINGS_OTA_PASS_CAP] = {0};
    app_settings_ota_get_ssid(ssid, sizeof(ssid));
    app_settings_ota_copy_password(pass, sizeof(pass));

    probe_note(WIFI_LINK_PROBE_RUNNING, ESP_OK, "joining service network");
    s_probe.address[0] = '\0';

    /* 20 s: long enough for a slow DHCP lease, short enough that a network
     * which will never answer does not strand the deck off its own AP. */
    esp_err_t rc = wifi_link_switch_to_sta(ssid, pass, 20000u);
    /* The passphrase has done its job; do not leave it on this stack. */
    memset(pass, 0, sizeof(pass));

    if (rc == ESP_OK) {
        esp_netif_ip_info_t ip = {0};
        if (s_sta_netif && esp_netif_get_ip_info(s_sta_netif, &ip) == ESP_OK) {
            snprintf(s_probe.address, sizeof(s_probe.address), IPSTR, IP2STR(&ip.ip));
        }
        probe_note(WIFI_LINK_PROBE_RUNNING, ESP_OK, "connected, returning to AP");
        /* Hold briefly so the address is observable on the deck's own display
         * before the AP comes back and the web client can read it. */
        vTaskDelay(pdMS_TO_TICKS(3000));
    } else {
        probe_note(WIFI_LINK_PROBE_RUNNING, rc,
                   rc == ESP_ERR_TIMEOUT ? "no address from network"
                                         : "association refused");
    }

    /* Unconditional, and the reason the whole probe exists: getting back. */
    esp_err_t back = wifi_link_restore_ap();
    if (back != ESP_OK) {
        probe_note(WIFI_LINK_PROBE_FAILED, back, "AP DID NOT COME BACK");
    } else if (rc == ESP_OK) {
        probe_note(WIFI_LINK_PROBE_OK, ESP_OK, "round trip complete");
    } else {
        s_probe.state = WIFI_LINK_PROBE_FAILED;   /* keep the failure detail */
    }

    s_probe_running = false;
    vTaskDelete(NULL);
}

esp_err_t wifi_link_probe_start(void)
{
    if (s_probe_running) return ESP_ERR_INVALID_STATE;
    if (!s_active || s_sta_mode) return ESP_ERR_INVALID_STATE;

    char ssid[APP_SETTINGS_OTA_SSID_CAP] = {0};
    app_settings_ota_get_ssid(ssid, sizeof(ssid));
    if (ssid[0] == '\0') return ESP_ERR_INVALID_ARG;

    s_probe_running = true;
    probe_note(WIFI_LINK_PROBE_RUNNING, ESP_OK, "starting");
    s_probe.address[0] = '\0';
    /* 5 KiB: the task itself does little, but wifi_link_switch_to_sta runs the
     * netif and association work on it. */
    if (xTaskCreate(wifi_link_probe_task, "wifi_probe", 5120, NULL, 4, NULL) != pdPASS) {
        s_probe_running = false;
        probe_note(WIFI_LINK_PROBE_FAILED, ESP_ERR_NO_MEM, "could not start task");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

wifi_link_probe_status_t wifi_link_probe_status(void)
{
    return s_probe;
}

esp_err_t wifi_link_stop(void)
{
    if (s_sta_mode) {
        esp_wifi_disconnect();
        s_sta_mode = false;
    }
    stop_sta_netif();
    stop_ap_services();
    stop_wifi_stack();
    stop_ap_netif();
    stop_hosted_transport();

    s_active = false;
    s_status.active = false;
    s_status.ap_clients = 0;
    ESP_LOGI(TAG, "Wi-Fi remote disabled");
    return ESP_OK;
}

static void wifi_link_worker(void *arg)
{
    (void)arg;
    /* Per-worker, not global: the worker exits once desired == active, so a
     * later operator request spawns a fresh one with a fresh budget. */
    wifi_link_retry_t retry;
    wifi_link_retry_reset(&retry);
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
            /* Breadcrumb before the risky part, then force it onto the card.
             * The journal writer only syncs every few seconds, so anything
             * still buffered is lost if the next call panics — which is
             * exactly the failure being chased here (the P4 occasionally
             * reboots when Wi-Fi is switched on, and the journal shows only
             * an unrelated last record because the buffer never reached the
             * card). Carries free internal heap, since the ESP-Hosted and
             * Wi-Fi bring-up is the largest internal allocation the firmware
             * ever makes. */
            service_log_event(SERVICE_LOG_WIFI_ENABLE_REQ, SERVICE_LOG_INFO,
                              2u,
                              (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                              (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                              0u, 0u, "internal free/largest");
            service_log_sync();

            esp_err_t start_rc = wifi_link_start();

            /* Stack high-water of this worker: bringing up ESP-Hosted, Wi-Fi
             * and httpd from a 6 KiB task is the other plausible cause of an
             * intermittent panic here, and this is the cheapest way to see how
             * close it runs. */
            uint32_t hw_words = uxTaskGetStackHighWaterMark(NULL);
            if (start_rc == ESP_OK) {
                service_log_event(SERVICE_LOG_WIFI_STARTED, SERVICE_LOG_INFO,
                                  2u,
                                  (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                                  hw_words, 0u, 0u, "internal free/stack words left");
            } else {
                service_log_event(SERVICE_LOG_WIFI_FAILED, SERVICE_LOG_ERROR,
                                  2u, (uint32_t)start_rc, hw_words, 0u, 0u,
                                  "rc/stack words left");
            }
            service_log_sync();

            if (start_rc != ESP_OK) {
                /* Without this the loop simply comes round again - active is
                 * still false, desired is still true - and re-runs the whole
                 * ESP-Hosted and Wi-Fi bring-up with no delay, forever. Bound
                 * it, back off, and after the third failure give up and leave
                 * the radio off until the operator asks again. */
                uint32_t wait_ms = wifi_link_retry_note_failure(&retry);
                if (wait_ms == 0u) {
                    ESP_LOGE(TAG, "Wi-Fi start failed %u times; giving up",
                             (unsigned)wifi_link_retry_attempts(&retry));
                    service_log_event(SERVICE_LOG_WIFI_FAILED, SERVICE_LOG_ERROR,
                                      2u, (uint32_t)start_rc,
                                      (uint32_t)wifi_link_retry_attempts(&retry),
                                      0u, 0u, "giving up, radio stays off");
                    service_log_sync();
                    /* Stop asking for it, so the loop can exit rather than
                     * spin: a further attempt needs a new operator request. */
                    xSemaphoreTake(s_ctrl_lock, portMAX_DELAY);
                    s_desired = false;
                    xSemaphoreGive(s_ctrl_lock);
                    /* Leave nothing half-initialised behind. */
                    wifi_link_stop();
                    continue;
                }
                ESP_LOGW(TAG, "Wi-Fi start failed (attempt %u); retrying in %u ms",
                         (unsigned)wifi_link_retry_attempts(&retry),
                         (unsigned)wait_ms);
                vTaskDelay(pdMS_TO_TICKS(wait_ms));
                continue;
            }
            wifi_link_retry_reset(&retry);
        } else {
            wifi_link_retry_reset(&retry);
            wifi_link_stop();
            service_log_event(SERVICE_LOG_WIFI_STOPPED, SERVICE_LOG_INFO,
                              1u,
                              (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                              0u, 0u, 0u, NULL);
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
