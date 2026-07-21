#pragma once

#include "esp_err.h"
#include "esp_log.h"

#define ESP_RETURN_ON_ERROR(x, tag, msg)             \
    do {                                             \
        esp_err_t __err_rc = (x);                    \
        if (__err_rc != ESP_OK) {                    \
            return __err_rc;                         \
        }                                            \
    } while (0)
