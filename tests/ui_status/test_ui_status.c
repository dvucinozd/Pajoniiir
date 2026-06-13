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

int main(void)
{
    test_formats_active_transport_text();

    puts("ui_status tests passed");
    return 0;
}
