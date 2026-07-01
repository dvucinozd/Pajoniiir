#pragma once

#include "deck_core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char effect[12];
    char beat[8];
    char target[8];
    char depth[8];
    char enabled[8];
} ui_beat_fx_overview_text_t;

void ui_beat_fx_format_overview(const deck_core_beat_fx_state_t *state,
                                ui_beat_fx_overview_text_t *out);

#ifdef __cplusplus
}
#endif
