#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/* Three i's plus the M3 product suffix, matching the operator-facing device
 * name. Keep the password separate: renaming the network must not silently
 * invalidate the accepted bench credential. */
#define WIFI_LINK_SOFTAP_SSID "Pajoniiir" /* runtime uses board SSID */
#define WIFI_LINK_PASSWORD    "Pajoniiir"

typedef enum {
    WIFI_LINK_MODE_OFF = 0,
    WIFI_LINK_MODE_STARTING,
    WIFI_LINK_MODE_AP,
    WIFI_LINK_MODE_STA,
    WIFI_LINK_MODE_RESTORING_AP,
    WIFI_LINK_MODE_STOPPING,
    WIFI_LINK_MODE_ERROR,
} wifi_link_mode_t;

typedef struct {
    bool initialized;
    bool active;            // SoftAP + web server currently running
    uint8_t ap_clients;
    esp_err_t last_error;
    wifi_link_mode_t mode;
    char ssid[33];
    char address[16];        // current AP or temporary STA IPv4
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

/*
 * One-shot connectivity probe: leave the AP, join the configured service
 * network, report the address obtained, come back.
 *
 * Exists so the transition can be proven on hardware before any download code
 * is built on top of it. It performs no HTTP and touches no firmware image; if
 * this cannot make the round trip reliably, nothing built above it would work
 * either, and the failure would be much harder to read.
 *
 * Runs on its own short-lived task because the round trip takes seconds. The
 * caller gets a copied snapshot rather than live state.
 */

typedef enum {
    WIFI_LINK_PROBE_IDLE = 0,
    WIFI_LINK_PROBE_RUNNING,
    WIFI_LINK_PROBE_OK,
    WIFI_LINK_PROBE_FAILED,
} wifi_link_probe_state_t;

typedef struct {
    wifi_link_probe_state_t state;
    esp_err_t last_error;
    char detail[48];     /* human-readable stage or failure */
    char address[16];    /* IPv4 obtained, empty when none */
} wifi_link_probe_status_t;

/* Rejected with ESP_ERR_INVALID_STATE if a probe is already running, and with
 * ESP_ERR_INVALID_ARG if no service network is configured. The caller is
 * responsible for refusing while audio is playing - wifi_link deliberately
 * knows nothing about decks. */
esp_err_t wifi_link_probe_start(void);

wifi_link_probe_status_t wifi_link_probe_status(void);

// One-time lightweight init (status + control mutex). Does NOT touch the radio.
// Call once at boot before wifi_link_start()/wifi_link_request_enable().
esp_err_t wifi_link_init(void);

// Bring the Wi-Fi remote up/down synchronously (ESP-Hosted + SoftAP + web
// server + captive DNS). Idempotent. Blocking (~1-2 s) — do NOT call from the
// LVGL task; use wifi_link_request_enable() from UI contexts instead.
// wifi_link_stop() returns ESP_ERR_INVALID_STATE while probe/OTA owns an
// AP->STA->AP transition; the async request path keeps the disable pending and
// applies it after the AP is restored.
esp_err_t wifi_link_start(void);
esp_err_t wifi_link_stop(void);

// Request enable/disable from any context (spawns a short worker task so the
// caller — e.g. the LVGL UI event callback — never blocks on the SDIO/C6
// bring-up). Rapid toggles collapse to the latest requested state.
void wifi_link_request_enable(bool enable);

// True while the SoftAP + web server are running.
bool wifi_link_is_active(void);

wifi_link_status_t wifi_link_get_status(void);
