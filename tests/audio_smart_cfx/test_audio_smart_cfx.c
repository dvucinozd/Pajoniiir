#include <assert.h>
#include <stdio.h>

#include "audio_filter.h"
#include "audio_smart_cfx.h"

static void test_center_stays_center(void)
{
    assert(audio_smart_cfx_curve_raw(AUDIO_FILTER_RAW_CENTER) == AUDIO_FILTER_RAW_CENTER);
}

static void test_near_center_is_softened(void)
{
    uint16_t input = AUDIO_FILTER_RAW_CENTER - 512u;
    uint16_t curved = audio_smart_cfx_curve_raw(input);
    assert(curved < AUDIO_FILTER_RAW_CENTER);
    assert(curved > input);
}

static void test_extremes_still_reach_extremes(void)
{
    assert(audio_smart_cfx_curve_raw(AUDIO_FILTER_RAW_MIN) == AUDIO_FILTER_RAW_MIN);
    assert(audio_smart_cfx_curve_raw(AUDIO_FILTER_RAW_MAX) == AUDIO_FILTER_RAW_MAX);
}

static void test_half_turn_is_half_effect(void)
{
    /* smoothstep passes through 1:1 at half turn — the mid-knob dead feel of
     * the old x^2 curve must not come back. */
    uint16_t half_low = AUDIO_FILTER_RAW_CENTER / 2u;
    uint16_t curved = audio_smart_cfx_curve_raw(half_low);
    assert(curved >= half_low - 8u);
    assert(curved <= half_low + 8u);

    uint16_t half_high = AUDIO_FILTER_RAW_CENTER +
                         ((AUDIO_FILTER_RAW_MAX - AUDIO_FILTER_RAW_CENTER) / 2u);
    curved = audio_smart_cfx_curve_raw(half_high);
    assert(curved >= half_high - 8u);
    assert(curved <= half_high + 8u);
}

static void test_past_half_turn_overshoots_linear(void)
{
    /* Past the midpoint the curve runs ahead of the raw knob so the kill
     * zone arrives with authority. */
    uint16_t three_quarters = (uint16_t)(AUDIO_FILTER_RAW_CENTER / 4u);
    uint16_t curved = audio_smart_cfx_curve_raw(three_quarters);
    assert(curved < three_quarters);
}

int main(void)
{
    test_center_stays_center();
    test_near_center_is_softened();
    test_extremes_still_reach_extremes();
    test_half_turn_is_half_effect();
    test_past_half_turn_overshoots_linear();
    puts("audio_smart_cfx tests passed");
    return 0;
}
