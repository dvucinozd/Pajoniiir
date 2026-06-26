#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ui_status.h"

static void test_formats_active_transport_text(void)
{
    ui_status_transport_text_t text;
    deck_state_t state = {0};

    ui_status_format_transport_text(&text, 1, &state, false, 0);
    assert(strcmp(text.text, "D2 PAUSE") == 0);
    assert(text.kind == UI_STATUS_TRANSPORT_PAUSE);

    state.playing = true;
    ui_status_format_transport_text(&text, 0, &state, false, 0);
    assert(strcmp(text.text, "D1 PLAY") == 0);
    assert(text.kind == UI_STATUS_TRANSPORT_PLAY);

    ui_status_format_transport_text(&text, 0, &state, true, 42);
    assert(strcmp(text.text, "D1 LOAD 42%") == 0);
    assert(text.kind == UI_STATUS_TRANSPORT_LOADING);
}

static void test_formats_limiter_telemetry_only_when_counter_increases(void)
{
    char text[16];
    audio_mixer_limiter_stats_t stats = {
        .limited_samples = 12,
        .positive_overloads = 7,
        .negative_overloads = 5,
        .peak_input_abs = 61000,
    };

    assert(ui_status_format_limiter_text(text, sizeof(text), &stats, 0));
    assert(strcmp(text, "CLIP 12") == 0);

    assert(!ui_status_format_limiter_text(text, sizeof(text), &stats, 12));

    stats.limited_samples = 0;
    assert(!ui_status_format_limiter_text(text, sizeof(text), &stats, 0));
    assert(!ui_status_format_limiter_text(NULL, 0, &stats, 0));
    assert(!ui_status_format_limiter_text(text, sizeof(text), NULL, 0));
}

int main(void)
{
    test_formats_active_transport_text();
    test_formats_limiter_telemetry_only_when_counter_increases();

    puts("ui_status tests passed");
    return 0;
}
