#pragma once

#define ESP_RETURN_ON_ERROR(expression, tag, message) do { \
    (void)(tag); (void)(message); \
    esp_err_t return_on_error_rc = (expression); \
    if (return_on_error_rc != ESP_OK) return return_on_error_rc; \
} while (0)

#define ESP_ERROR_CHECK(expression) do { \
    if ((expression) != ESP_OK) __builtin_trap(); \
} while (0)
