#pragma once

#include <stdbool.h>

#ifndef UI_DIAGNOSTICS_ENABLED
#define UI_DIAGNOSTICS_ENABLED 0
#endif

static inline bool ui_diagnostics_enabled(void)
{
    return UI_DIAGNOSTICS_ENABLED != 0;
}
