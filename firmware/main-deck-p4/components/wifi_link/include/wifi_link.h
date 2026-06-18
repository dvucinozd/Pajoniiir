#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define WIFI_LINK_SOFTAP_SSID "PAJONIIR"
#define WIFI_LINK_PASSWORD    "12345678"

typedef struct {
    bool initialized;
    uint8_t ap_clients;
    esp_err_t last_error;
    char ssid[33];
} wifi_link_status_t;

esp_err_t wifi_link_init(void);
wifi_link_status_t wifi_link_get_status(void);
