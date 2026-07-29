#pragma once

#include <stdint.h>

typedef uint32_t TickType_t;

#define pdTRUE  1
#define pdFALSE 0
#define pdPASS  1
#define pdFAIL  0

#define portMAX_DELAY 0xffffffffu
#define portTICK_PERIOD_MS 1u
#define pdMS_TO_TICKS(ms) (ms)

/* Critical sections and spinlocks are no-ops: level-1 suites are
 * single-threaded, so there is nothing to exclude. */
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(mux)  do { (void)(mux); } while (0)
#define portEXIT_CRITICAL(mux)   do { (void)(mux); } while (0)
#define taskENTER_CRITICAL(mux)  do { (void)(mux); } while (0)
#define taskEXIT_CRITICAL(mux)   do { (void)(mux); } while (0)
