#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ui_beat_fx_format.h"

static void test_default_state_formats_compact_overview_labels(void)
{
    deck_core_beat_fx_state_t state = {
        .effect = DECK_CORE_BEAT_FX_FILTER,
        .beat = DECK_CORE_BEAT_FX_BEAT_1,
        .target = CTRL_BEAT_FX_TARGET_BOTH,
        .depth = 64,
        .enabled = false,
    };
    ui_beat_fx_overview_text_t text = {0};

    ui_beat_fx_format_overview(&state, &text);

    assert(strcmp(text.effect, "FILTER") == 0);
    assert(strcmp(text.beat, "1") == 0);
    assert(strcmp(text.target, "BOTH") == 0);
    assert(strcmp(text.depth, "50%") == 0);
    assert(strcmp(text.enabled, "FX OFF") == 0);
}

static void test_enabled_echo_ch2_formats_on_state(void)
{
    deck_core_beat_fx_state_t state = {
        .effect = DECK_CORE_BEAT_FX_ECHO,
        .beat = DECK_CORE_BEAT_FX_BEAT_1_2,
        .target = CTRL_BEAT_FX_TARGET_CH2,
        .depth = 127,
        .enabled = true,
    };
    ui_beat_fx_overview_text_t text = {0};

    ui_beat_fx_format_overview(&state, &text);

    assert(strcmp(text.effect, "ECHO") == 0);
    assert(strcmp(text.beat, "1/2") == 0);
    assert(strcmp(text.target, "CH2") == 0);
    assert(strcmp(text.depth, "100%") == 0);
    assert(strcmp(text.enabled, "FX ON") == 0);
}

static void test_flanger_effect_formats_label(void)
{
    deck_core_beat_fx_state_t state = {
        .effect = DECK_CORE_BEAT_FX_FLANGER,
        .beat = DECK_CORE_BEAT_FX_BEAT_4,
        .target = CTRL_BEAT_FX_TARGET_CH1,
        .depth = 64,
        .enabled = true,
    };
    ui_beat_fx_overview_text_t text = {0};

    ui_beat_fx_format_overview(&state, &text);

    assert(strcmp(text.effect, "FLANGER") == 0);
    assert(strcmp(text.beat, "4") == 0);
    assert(strcmp(text.target, "CH1") == 0);
    assert(strcmp(text.enabled, "FX ON") == 0);
}

static void test_out_of_range_values_fallback_to_safe_text(void)
{
    deck_core_beat_fx_state_t state = {
        .effect = (deck_core_beat_fx_effect_t)99,
        .beat = (deck_core_beat_fx_beat_t)99,
        .target = (ctrl_beat_fx_target_t)99,
        .depth = 255,
        .enabled = false,
    };
    ui_beat_fx_overview_text_t text = {0};

    ui_beat_fx_format_overview(&state, &text);

    assert(strcmp(text.effect, "NONE") == 0);
    assert(strcmp(text.beat, "-") == 0);
    assert(strcmp(text.target, "-") == 0);
    assert(strcmp(text.depth, "100%") == 0);
}

int main(void)
{
    test_default_state_formats_compact_overview_labels();
    test_enabled_echo_ch2_formats_on_state();
    test_flanger_effect_formats_label();
    test_out_of_range_values_fallback_to_safe_text();
    puts("ui_beat_fx_format tests passed");
    return 0;
}
