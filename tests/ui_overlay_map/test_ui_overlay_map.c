#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "ui_overlay_map.h"

static void test_full_landscape_frame_maps_to_full_portrait_panel(void)
{
    ui_overlay_rect_t physical = {0};
    bool ok = ui_overlay_map_ppa270((ui_overlay_rect_t){0, 0, 800, 480},
                                    800, 480,
                                    &physical);

    assert(ok);
    assert(physical.x == 0);
    assert(physical.y == 0);
    assert(physical.w == 480);
    assert(physical.h == 800);
}

static void test_waveform_rect_rotates_into_panel_coordinates(void)
{
    ui_overlay_rect_t physical = {0};
    bool ok = ui_overlay_map_ppa270((ui_overlay_rect_t){92, 35, 560, 96},
                                    800, 480,
                                    &physical);

    assert(ok);
    assert(physical.x == 349);
    assert(physical.y == 92);
    assert(physical.w == 96);
    assert(physical.h == 560);
}

static void test_rejects_rect_outside_logical_canvas(void)
{
    ui_overlay_rect_t physical = {0};
    bool ok = ui_overlay_map_ppa270((ui_overlay_rect_t){780, 20, 40, 80},
                                    800, 480,
                                    &physical);

    assert(!ok);
}

static void test_converts_i8_canvas_to_rgb565_with_stride(void)
{
    const uint8_t indexed[] = {
        0, 1, 2, 9,
        2, 1, 0, 9,
    };
    const uint16_t palette[] = {
        0x0000, 0xF800, 0x07E0,
    };
    uint16_t rgb565[6] = {0};

    ui_overlay_i8_to_rgb565(indexed, 4, rgb565, 3, 3, 2, palette, 3);

    assert(rgb565[0] == 0x0000);
    assert(rgb565[1] == 0xF800);
    assert(rgb565[2] == 0x07E0);
    assert(rgb565[3] == 0x07E0);
    assert(rgb565[4] == 0xF800);
    assert(rgb565[5] == 0x0000);
}

int main(void)
{
    test_full_landscape_frame_maps_to_full_portrait_panel();
    test_waveform_rect_rotates_into_panel_coordinates();
    test_rejects_rect_outside_logical_canvas();
    test_converts_i8_canvas_to_rgb565_with_stride();

    puts("ui_overlay_map tests passed");
    return 0;
}
