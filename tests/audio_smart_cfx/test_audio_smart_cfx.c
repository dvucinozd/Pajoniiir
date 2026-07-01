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

int main(void)
{
    test_center_stays_center();
    test_near_center_is_softened();
    test_extremes_still_reach_extremes();
    puts("audio_smart_cfx tests passed");
    return 0;
}
