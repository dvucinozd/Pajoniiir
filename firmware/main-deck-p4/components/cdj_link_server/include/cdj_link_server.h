#pragma once

#include <stdint.h>
#include "esp_err.h"

#define CDJ_LINK_SERVER_PORT 8080u

esp_err_t cdj_link_server_start(void);
esp_err_t cdj_link_server_rebuild_library(void);
void cdj_link_server_clear_library(void);
uint32_t cdj_link_server_track_count(void);
