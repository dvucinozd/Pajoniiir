#pragma once

#include <stdint.h>

typedef uint32_t TickType_t;

#define pdTRUE  1
#define pdFALSE 0
#define pdPASS  1
#define pdFAIL  0

#define portMAX_DELAY 0xffffffffu
#define portTICK_PERIOD_MS 1u
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))

/* The model is single-threaded and never pre-empts, so a critical section has
 * nothing to exclude. The counter exists so a test can assert that firmware
 * left every section it entered. */
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0

void fake_rtos_critical_enter(void);
void fake_rtos_critical_exit(void);
int  fake_rtos_critical_depth(void);

#define portENTER_CRITICAL(mux)  do { (void)(mux); fake_rtos_critical_enter(); } while (0)
#define portEXIT_CRITICAL(mux)   do { (void)(mux); fake_rtos_critical_exit();  } while (0)
#define taskENTER_CRITICAL(mux)  portENTER_CRITICAL(mux)
#define taskEXIT_CRITICAL(mux)   portEXIT_CRITICAL(mux)
