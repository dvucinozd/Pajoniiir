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

static void test_pagination_bounds_large_library_to_eight_rows(void)
{
    ui_library_page_t first = ui_library_page_for_selection(1024, 0);
    assert(first.first_index == 0);
    assert(first.row_count == UI_LIBRARY_PAGE_ROWS);
    assert(first.selected_row == 0);
    assert(first.page_index == 0);
    assert(first.page_count == 128);

    ui_library_page_t second = ui_library_page_for_selection(1024, 8);
    assert(second.first_index == 8);
    assert(second.row_count == UI_LIBRARY_PAGE_ROWS);
    assert(second.selected_row == 0);
    assert(second.page_index == 1);

    ui_library_page_t last = ui_library_page_for_selection(1024, 1023);
    assert(last.first_index == 1016);
    assert(last.row_count == UI_LIBRARY_PAGE_ROWS);
    assert(last.selected_row == 7);
    assert(last.page_index == 127);
    assert(last.page_count == 128);
}

static void test_pagination_clamps_partial_and_empty_pages(void)
{
    ui_library_page_t partial = ui_library_page_for_selection(10, 99);
    assert(partial.first_index == 8);
    assert(partial.row_count == 2);
    assert(partial.selected_row == 1);
    assert(ui_library_page_absolute_index(&partial, 0) == 8);
    assert(ui_library_page_absolute_index(&partial, 1) == 9);
    assert(ui_library_page_absolute_index(&partial, 2) == -1);

    ui_library_page_t empty = ui_library_page_for_selection(0, 5);
    assert(empty.first_index == 0);
    assert(empty.row_count == 0);
    assert(empty.page_count == 0);
    assert(ui_library_page_absolute_index(&empty, 0) == -1);
}

static void test_page_delta_keeps_relative_row_and_clamps_edges(void)
{
    assert(ui_library_page_selection_after_delta(20, 3, 1) == 11);
    assert(ui_library_page_selection_after_delta(20, 11, 1) == 19);
    assert(ui_library_page_selection_after_delta(20, 19, 1) == 19);
    assert(ui_library_page_selection_after_delta(20, 11, -1) == 3);
    assert(ui_library_page_selection_after_delta(20, 3, -1) == 3);
    assert(ui_library_page_selection_after_delta(0, 0, 1) == 0);
}

int main(void)
{
    test_row_format_truncates_long_text_and_formats_duration();
    test_row_format_uses_safe_empty_text_for_nulls();
    test_update_plan_uses_flags_and_active_tab();
    test_pagination_bounds_large_library_to_eight_rows();
    test_pagination_clamps_partial_and_empty_pages();
    test_page_delta_keeps_relative_row_and_clamps_edges();

    puts("ui_library tests passed");
    return 0;
}
