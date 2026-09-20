
#pragma once
typedef int portMUX_TYPE;
typedef unsigned UBaseType_t;
#define portMUX_INITIALIZER_UNLOCKED 0
void review_enter_critical(void);
#define portENTER_CRITICAL(mux) ((void)(mux), review_enter_critical())
#define portEXIT_CRITICAL(mux) ((void)(mux))
