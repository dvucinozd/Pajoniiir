#pragma once

#include "esp_err.h"

esp_err_t sd_diag_log_init(void);
esp_err_t sd_diag_log_write(const char *tag, const char *message);

