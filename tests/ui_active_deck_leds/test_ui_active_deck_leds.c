#include <assert.h>
#include <stdio.h>

#include "ui_active_deck_leds.h"

static void test_play_cue_beat_and_end_from_active_deck_state(void)
{
    ui_active_deck_leds_t leds =
        ui_active_deck_leds_calculate(true, 5000, 5000, 12000, true, 125);

    assert(leds.play == 1);
    assert(leds.cue == 1);
    assert(leds.beat == 1);
    assert(leds.end == 1);
}

static void test_leds_clear_when_active_deck_is_paused_and_not_at_cue_or_end(void)
{
    ui_active_deck_leds_t leds =
        ui_active_deck_leds_calculate(false, 2000, 1000, 120000, true, 50);

    assert(leds.play == 0);
    assert(leds.cue == 0);
    assert(leds.beat == 0);
    assert(leds.end == 0);
}

static void test_end_led_requires_known_duration(void)
{
    ui_active_deck_leds_t leds =
        ui_active_deck_leds_calculate(true, 90000, 0, 0, false, 0);

    assert(leds.play == 1);
    assert(leds.end == 0);
}

int main(void)
{
    test_play_cue_beat_and_end_from_active_deck_state();
    test_leds_clear_when_active_deck_is_paused_and_not_at_cue_or_end();
    test_end_led_requires_known_duration();

    puts("ui_active_deck_leds tests passed");
    return 0;
}
