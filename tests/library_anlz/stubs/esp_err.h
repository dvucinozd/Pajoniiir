#pragma once

#ifndef ESP_ERR_T_DEFINED
typedef int esp_err_t;
#define ESP_ERR_T_DEFINED
#endif

#ifndef ESP_OK
#define ESP_OK 0
#endif
#ifndef ESP_FAIL
#define ESP_FAIL -1
#endif
#define ESP_ERR_NO_MEM           0x101
#define ESP_ERR_INVALID_ARG      0x102
#define ESP_ERR_INVALID_STATE    0x103
#define ESP_ERR_NOT_FOUND        0x105
#define ESP_ERR_NOT_SUPPORTED    0x106
#define ESP_ERR_INVALID_RESPONSE 0x108

static inline const char *esp_err_to_name(esp_err_t err)
{
    (void)err;
    return "ESP";
}
