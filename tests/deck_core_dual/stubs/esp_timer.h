#pragma once
#include <stdint.h>
#include <time.h>

static inline int64_t esp_timer_get_time(void)
{
    return (int64_t)clock() * 1000000 / CLOCKS_PER_SEC;
}
