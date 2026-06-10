#pragma once

#include <stdint.h>

typedef uint32_t TickType_t;

#define pdTRUE 1
#define pdPASS 1
#define portMAX_DELAY 0xffffffffu
#define portTICK_PERIOD_MS 1u
#define pdMS_TO_TICKS(ms) (ms)
