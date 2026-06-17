#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define WIFI_LINK_SOFTAP_PREFIX "PAJONIIR"
#define WIFI_LINK_PASSWORD      "12345678"

typedef enum {
    WIFI_LINK_MODE_OFF = 0,
    WIFI_LINK_MODE_HOST = 1,
    WIFI_LINK_MODE_JOIN = 2,
} wifi_link_mode_t;

typedef struct {
    wifi_link_mode_t mode;
    bool initialized;
    bool connected;
    bool scanning;
    uint8_t ap_clients;
    uint32_t retry_count;
    esp_err_t last_error;
    char peer_id[16];
    char ssid[33];
} wifi_link_status_t;

esp_err_t wifi_link_init(wifi_link_mode_t mode);
wifi_link_status_t wifi_link_get_status(void);
const char *wifi_link_peer_id(void);
const char *wifi_link_ssid(void);
