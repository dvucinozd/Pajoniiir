#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/* Three i's, matching the product name, the mDNS hostname `pajoniiir.local` and
 * the update host. This shipped as PAJONIIR (two) until 2026-07-24, which is
 * exactly the kind of near-miss that later produces a wrong URL by hand. */
#define WIFI_LINK_SOFTAP_SSID "PAJONIIIR"
#define WIFI_LINK_PASSWORD    "PajoNiiiR"

typedef struct {
    bool initialized;
    bool active;            // SoftAP + web server currently running
    uint8_t ap_clients;
    esp_err_t last_error;
    char ssid[33];
} wifi_link_status_t;

/*
 * Temporary client mode, for checking and downloading updates.
 *
 * The SoftAP is the normal operating mode and the only guaranteed service
 * surface; STA is a visit, not a state to live in. Every exit from it - success
 * or failure - must end back on the AP, because the AP is how the deck is
 * reached at all.
 *
 * Both calls are blocking and must run on the wifi_link worker, never on a UI
 * or HTTP handler: association and DHCP take seconds.
 */

/* Stop the AP service and associate with `ssid`. The ESP-Hosted transport and
 * the Wi-Fi stack stay up across the switch; only the AP interface and its
 * services are torn down.
 *
 * `password` may be NULL or empty for an open network. Returns ESP_OK only
 * once an IP address has actually been assigned - association alone is not
 * enough to fetch anything. On any failure the caller must still call
 * wifi_link_restore_ap(); this function does not restore on its own, so the
 * caller can log the specific failure first.
 */
esp_err_t wifi_link_switch_to_sta(const char *ssid, const char *password,
                                  uint32_t timeout_ms);

/* Tear down STA and bring the SoftAP, captive DNS and web service back.
 * Safe to call whether or not the switch succeeded. */
esp_err_t wifi_link_restore_ap(void);

/* True while the deck is on the service network rather than its own AP. */
bool wifi_link_is_sta(void);

// One-time lightweight init (status + control mutex). Does NOT touch the radio.
// Call once at boot before wifi_link_start()/wifi_link_request_enable().
esp_err_t wifi_link_init(void);

// Bring the Wi-Fi remote up/down synchronously (ESP-Hosted + SoftAP + web
// server + captive DNS). Idempotent. Blocking (~1-2 s) — do NOT call from the
// LVGL task; use wifi_link_request_enable() from UI contexts instead.
esp_err_t wifi_link_start(void);
esp_err_t wifi_link_stop(void);

// Request enable/disable from any context (spawns a short worker task so the
// caller — e.g. the LVGL UI event callback — never blocks on the SDIO/C6
// bring-up). Rapid toggles collapse to the latest requested state.
void wifi_link_request_enable(bool enable);

// True while the SoftAP + web server are running.
bool wifi_link_is_active(void);

wifi_link_status_t wifi_link_get_status(void);
