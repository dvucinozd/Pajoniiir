#pragma once

#include <stdbool.h>
#include "esp_err.h"

typedef enum {
    WIFI_TRANSITION_OWNER_NONE = 0,
    WIFI_TRANSITION_OWNER_PROBE,
    WIFI_TRANSITION_OWNER_OTA,
} wifi_transition_owner_t;

esp_err_t wifi_transition_lease_acquire(wifi_transition_owner_t owner);
void wifi_transition_lease_release(wifi_transition_owner_t owner);
wifi_transition_owner_t wifi_transition_lease_owner(void);
