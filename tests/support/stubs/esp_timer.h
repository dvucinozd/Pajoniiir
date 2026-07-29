#pragma once

#include <stdint.h>

/* Synthetic monotonic timeline rather than the host clock, so a run is
 * reproducible and a suite that only needs "time moves forward" gets that
 * without depending on how fast the machine is. Each read advances by 1 ms.
 *
 * A suite that needs real elapsed time keeps its own esp_timer.h ahead of this
 * one on the include path (tests/deck_core_dual does). A suite that needs to
 * control the timeline explicitly should use tests/support/rtos, whose tick and
 * timer advance together under test control. */

static inline int64_t esp_timer_get_time(void)
{
    static int64_t now_us = 0;
    now_us += 1000;
    return now_us;
}
