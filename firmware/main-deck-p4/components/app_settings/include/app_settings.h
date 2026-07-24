#pragma once
//
// app_settings — persistent user settings stored in NVS.
//
// Survives reboots. Values are cached in RAM; setters write through to NVS
// immediately (small, infrequent writes). Firmware-only (uses NVS); the PC
// simulator should not link this.
//
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    uint8_t audio_out;      // legacy NVS key: 0 = monitor speaker PA on, 1 = headphones / speaker PA off
    uint8_t backlight_pct;  // 10..100
    uint8_t time_remain;    // 0 = elapsed time, 1 = remaining time
    uint8_t cue_mode;       // 0 = stereo master, 1 = split mono (L=master, R=cue)
    uint8_t master_trim_preset; // 0 = 0 dB, 1 = -3 dB, 2 = -6 dB
    uint8_t wifi_remote;    // 0 = Wi-Fi remote off (default), 1 = on (SoftAP + web UI)
} app_settings_t;

// Initialise NVS and load saved settings (or sensible defaults if none stored).
esp_err_t      app_settings_init(void);

// Snapshot of the current settings.
app_settings_t app_settings_get(void);

// Update one setting and persist it to NVS.
void app_settings_set_audio_out(uint8_t out);
void app_settings_set_backlight(uint8_t pct);
void app_settings_set_time_remain(uint8_t remain);
void app_settings_set_cue_mode(uint8_t mode);
void app_settings_set_master_trim_preset(uint8_t preset);
void app_settings_set_wifi_remote(uint8_t on);

/* ── Pull-OTA service network ─────────────────────────────────────────────
 *
 * Deliberately NOT part of app_settings_t. That struct is returned by value
 * and its whole contents are logged at boot; a passphrase inside it would
 * leak through both. These accessors keep the secret reachable only by the
 * code that actually associates.
 *
 * Validate with p4_ota_pull_config.h before calling the setter - this layer
 * stores what it is given.
 */
#define APP_SETTINGS_OTA_SSID_CAP 33u   /* 32 + NUL */
#define APP_SETTINGS_OTA_PASS_CAP 65u   /* 64 + NUL */
#define APP_SETTINGS_OTA_URL_CAP  161u

/* Copy the SSID / base URL out. Safe to show in the UI and in status. */
void app_settings_ota_get_ssid(char *out, size_t cap);
void app_settings_ota_get_url(char *out, size_t cap);

/* Whether a passphrase is stored. This is what status APIs report; there is
 * deliberately no way to read the passphrase back out over the network. */
bool app_settings_ota_has_password(void);

/* For the Wi-Fi association path only. Never log, never serialise, never
 * return over an API. */
void app_settings_ota_copy_password(char *out, size_t cap);

/* Persist. A NULL password keeps whatever is stored, so the SSID or URL can
 * be corrected without retyping the passphrase; an empty string clears it
 * (open network). */
esp_err_t app_settings_ota_set(const char *ssid, const char *password, const char *url);

/* Forget the network entirely - the CLEAR WI-FI action. */
void app_settings_ota_clear(void);
