#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Counted so tests/run_p4_host_tests.ps1 can pin the number of assertions this
 * suite executes. A test function that is deleted or commented out lowers the
 * count and fails the run; the previous guard grepped this file for the names of
 * its own test functions, which proved only that the text was present. */
static unsigned s_checks;
#define CHECK(expr) do {                                                     \
    s_checks++;                                                              \
    if (!(expr)) {                                                           \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr);      \
        abort();                                                             \
    }                                                                        \
} while (0)


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

    CHECK(strcmp(out.title, "A very long title that ...") == 0);
    CHECK(strcmp(out.artist, "An artist name ...") == 0);
    CHECK(strcmp(out.key, "8A") == 0);
    CHECK(strcmp(out.bpm, "128") == 0);
    CHECK(strcmp(out.duration, "6:07") == 0);
}

static void test_row_format_uses_safe_empty_text_for_nulls(void)
{
    ui_library_row_text_t out;

    ui_library_format_row_text(&out, NULL, NULL, NULL, 0, 0);

    CHECK(strcmp(out.title, "") == 0);
    CHECK(strcmp(out.artist, "") == 0);
    CHECK(strcmp(out.key, "") == 0);
    CHECK(strcmp(out.bpm, "0") == 0);
    CHECK(strcmp(out.duration, "0:00") == 0);
}

static void test_update_plan_uses_flags_and_active_tab(void)
{
    ui_library_update_plan_t idle = ui_library_plan_update(0, false, false);
    CHECK(!idle.apply_usb_removed);
    CHECK(idle.poll_track_load_result);
    CHECK(!idle.refresh_library);
    CHECK(!idle.focus_library_table);

    ui_library_update_plan_t library = ui_library_plan_update(1, true, true);
    CHECK(library.apply_usb_removed);
    CHECK(library.poll_track_load_result);
    CHECK(library.refresh_library);
    CHECK(library.focus_library_table);
}

static void test_pagination_bounds_large_library_to_eight_rows(void)
{
    ui_library_page_t first = ui_library_page_for_selection(1024, 0);
    CHECK(first.first_index == 0);
    CHECK(first.row_count == UI_LIBRARY_PAGE_ROWS);
    CHECK(first.selected_row == 0);
    CHECK(first.page_index == 0);
    CHECK(first.page_count == 128);

    ui_library_page_t second = ui_library_page_for_selection(1024, 8);
    CHECK(second.first_index == 8);
    CHECK(second.row_count == UI_LIBRARY_PAGE_ROWS);
    CHECK(second.selected_row == 0);
    CHECK(second.page_index == 1);

    ui_library_page_t last = ui_library_page_for_selection(1024, 1023);
    CHECK(last.first_index == 1016);
    CHECK(last.row_count == UI_LIBRARY_PAGE_ROWS);
    CHECK(last.selected_row == 7);
    CHECK(last.page_index == 127);
    CHECK(last.page_count == 128);
}

static void test_pagination_clamps_partial_and_empty_pages(void)
{
    ui_library_page_t partial = ui_library_page_for_selection(10, 99);
    CHECK(partial.first_index == 8);
    CHECK(partial.row_count == 2);
    CHECK(partial.selected_row == 1);
    CHECK(ui_library_page_absolute_index(&partial, 0) == 8);
    CHECK(ui_library_page_absolute_index(&partial, 1) == 9);
    CHECK(ui_library_page_absolute_index(&partial, 2) == -1);

    ui_library_page_t empty = ui_library_page_for_selection(0, 5);
    CHECK(empty.first_index == 0);
    CHECK(empty.row_count == 0);
    CHECK(empty.page_count == 0);
    CHECK(ui_library_page_absolute_index(&empty, 0) == -1);
}

static void test_page_delta_keeps_relative_row_and_clamps_edges(void)
{
    CHECK(ui_library_page_selection_after_delta(20, 3, 1) == 11);
    CHECK(ui_library_page_selection_after_delta(20, 11, 1) == 19);
    CHECK(ui_library_page_selection_after_delta(20, 19, 1) == 19);
    CHECK(ui_library_page_selection_after_delta(20, 11, -1) == 3);
    CHECK(ui_library_page_selection_after_delta(20, 3, -1) == 3);
    CHECK(ui_library_page_selection_after_delta(0, 0, 1) == 0);
}

int main(void)
{
    test_row_format_truncates_long_text_and_formats_duration();
    test_row_format_uses_safe_empty_text_for_nulls();
    test_update_plan_uses_flags_and_active_tab();
    test_pagination_bounds_large_library_to_eight_rows();
    test_pagination_clamps_partial_and_empty_pages();
    test_page_delta_keeps_relative_row_and_clamps_edges();

    printf("TESTS_RUN=%u\n", s_checks);
    puts("ui_library tests passed");
    return 0;
}
