#include "ui_beat_fx_format.h"

#include <stdio.h>
#include <string.h>

static const char *ui_beat_fx_effect_text(deck_core_beat_fx_effect_t effect)
{
    switch (effect) {
    case DECK_CORE_BEAT_FX_FILTER: return "FILTER";
    case DECK_CORE_BEAT_FX_ECHO: return "ECHO";
    case DECK_CORE_BEAT_FX_FLANGER: return "FLANG";
    case DECK_CORE_BEAT_FX_DELAY: return "DELAY";
    case DECK_CORE_BEAT_FX_NONE:
    case DECK_CORE_BEAT_FX_COUNT:
    default:
        return "NONE";
    }
}

static const char *ui_beat_fx_beat_text(deck_core_beat_fx_beat_t beat)
{
    switch (beat) {
    case DECK_CORE_BEAT_FX_BEAT_1_4: return "1/4";
    case DECK_CORE_BEAT_FX_BEAT_1_2: return "1/2";
    case DECK_CORE_BEAT_FX_BEAT_1: return "1";
    case DECK_CORE_BEAT_FX_BEAT_2: return "2";
    case DECK_CORE_BEAT_FX_BEAT_4: return "4";
    case DECK_CORE_BEAT_FX_BEAT_COUNT:
    default:
        return "-";
    }
}

static const char *ui_beat_fx_target_text(ctrl_beat_fx_target_t target)
{
    switch (target) {
    case CTRL_BEAT_FX_TARGET_CH1: return "CH1";
    case CTRL_BEAT_FX_TARGET_CH2: return "CH2";
    case CTRL_BEAT_FX_TARGET_BOTH: return "BOTH";
    default:
        return "-";
    }
}

void ui_beat_fx_format_overview(const deck_core_beat_fx_state_t *state,
                                ui_beat_fx_overview_text_t *out)
{
    if (!out) {
        return;
    }

    memset(out, 0, sizeof(*out));
    if (!state) {
        snprintf(out->effect, sizeof(out->effect), "NONE");
        snprintf(out->beat, sizeof(out->beat), "-");
        snprintf(out->target, sizeof(out->target), "-");
        snprintf(out->depth, sizeof(out->depth), "0%%");
        snprintf(out->enabled, sizeof(out->enabled), "FX OFF");
        return;
    }

    unsigned depth = state->depth;
    if (depth > 127u) {
        depth = 127u;
    }
    unsigned depth_pct = (depth * 100u + 63u) / 127u;

    snprintf(out->effect, sizeof(out->effect), "%s", ui_beat_fx_effect_text(state->effect));
    snprintf(out->beat, sizeof(out->beat), "%s", ui_beat_fx_beat_text(state->beat));
    snprintf(out->target, sizeof(out->target), "%s", ui_beat_fx_target_text(state->target));
    snprintf(out->depth, sizeof(out->depth), "%u%%", depth_pct);
    snprintf(out->enabled, sizeof(out->enabled), "%s", state->enabled ? "FX ON" : "FX OFF");
}
