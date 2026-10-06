#pragma once
#include "esp_err.h"
#include "esp_netif.h"
esp_err_t board_ethernet_start(void);
esp_netif_t *board_ethernet_netif(void);
