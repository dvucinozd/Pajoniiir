#pragma once

#include <stdbool.h>
#include <stdint.h>

#if defined(MEDIA_IO_GATE_STANDALONE_TEST)
#ifndef ESP_ERR_T_DEFINED
typedef int esp_err_t;
#define ESP_ERR_T_DEFINED
#endif
#define ESP_OK 0
#define ESP_FAIL -1
#else
#include "esp_err.h"
#endif

esp_err_t media_io_gate_init(void);
void media_io_gate_begin(void);
bool media_io_gate_try_begin(uint32_t timeout_ms);
void media_io_gate_end(void);
