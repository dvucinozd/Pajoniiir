#pragma once

#include <stdint.h>

/* Monotonic-ish stub: advances by 1 ms each call. */
static inline int64_t esp_timer_get_time(void)
{
    static int64_t t = 0;
    t += 1000;
    return t;
}
