#pragma once
#include "esp_err.h"

#define ESP_RETURN_ON_ERROR(x, log_tag, format, ...) do { \
    esp_err_t err_rc_ = (x); \
    if (err_rc_ != ESP_OK) { \
        return err_rc_; \
    } \
} while(0)

#define ESP_RETURN_ON_FALSE(x, err, log_tag, format, ...) do { \
    if (!(x)) { \
        return (err); \
    } \
} while(0)

#define ESP_GOTO_ON_ERROR(x, goto_tag, log_tag, format, ...) do { \
    esp_err_t err_rc_ = (x); \
    if (err_rc_ != ESP_OK) { \
        goto goto_tag; \
    } \
} while(0)
