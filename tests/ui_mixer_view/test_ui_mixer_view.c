#include <assert.h>
#include <stdio.h>

#include "ui_mixer_view.h"

static void test_fader_percent_and_pfl_state_are_exposed(void)
{
    ui_mixer_deck_view_t view =
        ui_mixer_deck_view_from_state(8192, 0.5f, true);

    assert(view.fader_pct == 50);
    assert(view.output_pct == 50);
    assert(view.pfl_on);
}

static void test_crossfader_knob_clamps_to_track_width(void)
{
    assert(ui_mixer_crossfader_knob_x(0, 100) == 0);
    assert(ui_mixer_crossfader_knob_x(8192, 100) == 50);
    assert(ui_mixer_crossfader_knob_x(16383, 100) == 100);
    assert(ui_mixer_crossfader_knob_x(20000, 100) == 100);
}

int main(void)
{
    test_fader_percent_and_pfl_state_are_exposed();
    test_crossfader_knob_clamps_to_track_width();

    puts("ui_mixer_view tests passed");
    return 0;
}
