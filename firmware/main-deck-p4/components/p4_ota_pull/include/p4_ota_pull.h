#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "p4_ota_pull_manifest.h"

/*
 * Update check over the temporary STA visit.
 *
 * Owns the whole round trip: leave the AP, fetch the channel document, parse
 * it, come back. Deliberately stops there - it reports what is available and
 * flashes nothing. Downloading and installing is a separate, explicitly
 * confirmed step, because an update that installs itself the moment it is
 * noticed is exactly what a DJ does not want mid-set.
 *
 * Depends on wifi_link; wifi_link does not depend on this, so there is no
 * cycle. The web layer reaches it through app_main, for the same reason.
 */

typedef enum {
    P4_OTA_PULL_IDLE = 0,
    P4_OTA_PULL_CHECKING,
    P4_OTA_PULL_UP_TO_DATE,
    P4_OTA_PULL_AVAILABLE,
    P4_OTA_PULL_FAILED,
} p4_ota_pull_state_t;

typedef struct {
    p4_ota_pull_state_t state;
    esp_err_t last_error;
    char detail[64];                        /* stage or failure, operator-facing */
    char available_release[P4_OTA_PULL_RELEASE_MAX + 1u];
    uint32_t available_size;
} p4_ota_pull_status_t;

/* Rejected with ESP_ERR_INVALID_ARG when no update URL is configured, and with
 * ESP_ERR_INVALID_STATE when a check is already running or Wi-Fi is off. The
 * caller must refuse while audio is playing - this component knows nothing
 * about decks. */
esp_err_t p4_ota_pull_check_start(void);

p4_ota_pull_status_t p4_ota_pull_get_status(void);
