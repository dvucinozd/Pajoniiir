#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "ui_performance_tabs.h"

static void test_jump_target_uses_nearest_beatgrid_entry(void)
{
    const uint32_t beats[] = {1000, 2000, 3000, 4000};
    uint32_t target = ui_performance_tabs_calculate_jump_target(2200, 120, 1, beats, 4);
    assert(target == 3000);
}

static void test_jump_target_clamps_beatgrid_edges(void)
{
    const uint32_t beats[] = {1000, 2000, 3000, 4000};
    assert(ui_performance_tabs_calculate_jump_target(900, 120, -8, beats, 4) == 1000);
    assert(ui_performance_tabs_calculate_jump_target(3800, 120, 8, beats, 4) == 4000);
}

static void test_jump_target_falls_back_to_bpm_and_clamps_zero(void)
{
    assert(ui_performance_tabs_calculate_jump_target(1000, 120, 4, NULL, 0) == 3000);
    assert(ui_performance_tabs_calculate_jump_target(1000, 120, -8, NULL, 0) == 0);
    assert(ui_performance_tabs_calculate_jump_target(1000, 0, 1, NULL, 0) == 1500);
}

int main(void)
{
    test_jump_target_uses_nearest_beatgrid_entry();
    test_jump_target_clamps_beatgrid_edges();
    test_jump_target_falls_back_to_bpm_and_clamps_zero();
    puts("ui_performance_tabs tests passed");
    return 0;
}
