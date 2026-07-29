#pragma once

#include "esp_err.h"
#include "esp_log.h"

/* Variadic tail so both the two-argument and the printf-style call sites in the
 * firmware compile against the same macro. */

#define ESP_RETURN_ON_ERROR(x, log_tag, ...) do { \
    esp_err_t err_rc_ = (x);                      \
    if (err_rc_ != ESP_OK) {                      \
        return err_rc_;                           \
    }                                             \
} while (0)

#define ESP_RETURN_ON_FALSE(x, err, log_tag, ...) do { \
    if (!(x)) {                                        \
        return (err);                                  \
    }                                                  \
} while (0)

#define ESP_GOTO_ON_ERROR(x, goto_tag, log_tag, ...) do { \
    esp_err_t err_rc_ = (x);                              \
    if (err_rc_ != ESP_OK) {                              \
        goto goto_tag;                                    \
    }                                                     \
} while (0)

#define ESP_GOTO_ON_FALSE(x, err, goto_tag, log_tag, ...) do { \
    if (!(x)) {                                                \
        ret = (err);                                           \
        goto goto_tag;                                         \
    }                                                          \
} while (0)
