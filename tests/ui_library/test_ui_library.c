#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ui_library.h"

static void test_row_format_truncates_long_text_and_formats_duration(void)
{
    ui_library_row_text_t out;

    ui_library_format_row_text(&out,
                               "A very long title that should be shortened",
                               "An artist name beyond the compact column",
                               "8A",
                               128,
                               367000);

    assert(strcmp(out.title, "A very long title that ...") == 0);
    assert(strcmp(out.artist, "An artist name ...") == 0);
    assert(strcmp(out.key, "8A") == 0);
    assert(strcmp(out.bpm, "128") == 0);
    assert(strcmp(out.duration, "6:07") == 0);
}

static void test_row_format_uses_safe_empty_text_for_nulls(void)
{
    ui_library_row_text_t out;

    ui_library_format_row_text(&out, NULL, NULL, NULL, 0, 0);

    assert(strcmp(out.title, "") == 0);
    assert(strcmp(out.artist, "") == 0);
    assert(strcmp(out.key, "") == 0);
    assert(strcmp(out.bpm, "0") == 0);
    assert(strcmp(out.duration, "0:00") == 0);
}

static void test_update_plan_uses_flags_and_active_tab(void)
{
    ui_library_update_plan_t idle = ui_library_plan_update(0, false, false);
    assert(!idle.apply_usb_removed);
    assert(idle.poll_track_load_result);
    assert(!idle.refresh_library);
    assert(!idle.focus_library_table);

    ui_library_update_plan_t library = ui_library_plan_update(1, true, true);
    assert(library.apply_usb_removed);
    assert(library.poll_track_load_result);
    assert(library.refresh_library);
    assert(library.focus_library_table);
}

int main(void)
{
    test_row_format_truncates_long_text_and_formats_duration();
    test_row_format_uses_safe_empty_text_for_nulls();
    test_update_plan_uses_flags_and_active_tab();

    puts("ui_library tests passed");
    return 0;
}
