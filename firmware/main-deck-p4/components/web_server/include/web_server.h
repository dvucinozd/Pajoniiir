#pragma once

#include "esp_err.h"

esp_err_t web_server_start(void);
void web_server_stop(void);

esp_err_t dns_server_start(void);
void dns_server_stop(void);
