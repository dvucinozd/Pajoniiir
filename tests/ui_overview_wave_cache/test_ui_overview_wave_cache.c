#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ui_overview_wave_cache.h"

static const uint16_t palette[] = {
    0x0000, 0xF16E, 0x235F, 0x475C, 0xE71D,
    0x1F32, 0xFD66, 0x9ADF, 0x3989,
};

static int count_changed_pixels(const uint16_t *a, const uint16_t *b, int count)
{
    int changed = 0;
    for (int i = 0; i < count; i++) {
        if (a[i] != b[i]) {
            changed++;
        }
    }
    return changed;
}

static void test_initial_update_renders_full_view(void)
{
    uint8_t samples[64];
    memset(samples, 0x1Fu, sizeof(samples));
    ui_waveform_source_t source = {
        .kind = UI_WAVEFORM_SOURCE_HIGH,
        .samples = samples,
        .sample_count = sizeof(samples),
    };
    uint16_t pixels[16 * 12] = {0};
    ui_overview_wave_cache_t cache = {0};
    ui_overview_wave_cache_report_t report;

    ui_overview_wave_cache_reset(&cache);
    assert(ui_overview_wave_cache_bind(&cache, pixels, 16, 16, 12,
                                       palette, sizeof(palette) / sizeof(palette[0])));
    assert(ui_overview_wave_cache_update(&cache, &source, 64000, NULL,
                                         32000, 16000, &report));

    assert(report.kind == UI_OVERVIEW_WAVE_CACHE_FULL);
    assert(report.scroll_dx_px == 0);
    assert(report.columns_rendered == 16);
    assert(report.blit_required);
    assert(cache.valid);
}

static void test_small_center_advance_scrolls_cached_view(void)
{
    uint8_t samples[256];
    for (int i = 0; i < 256; i++) {
        samples[i] = (uint8_t)(0x10u + (i & 0x0Fu));
    }
    ui_waveform_source_t source = {
        .kind = UI_WAVEFORM_SOURCE_HIGH,
        .samples = samples,
        .sample_count = sizeof(samples),
    };
    uint16_t pixels[16 * 12] = {0};
    uint16_t before[16 * 12] = {0};
    ui_overview_wave_cache_t cache = {0};
    ui_overview_wave_cache_report_t report;

    ui_overview_wave_cache_reset(&cache);
    assert(ui_overview_wave_cache_bind(&cache, pixels, 16, 16, 12,
                                       palette, sizeof(palette) / sizeof(palette[0])));
    assert(ui_overview_wave_cache_update(&cache, &source, 64000, NULL,
                                         32000, 16000, &report));
    memcpy(before, pixels, sizeof(before));

    assert(ui_overview_wave_cache_update(&cache, &source, 64000, NULL,
                                         33000, 16000, &report));

    assert(report.kind == UI_OVERVIEW_WAVE_CACHE_SCROLL);
    assert(report.scroll_dx_px > 0);
    assert(report.columns_rendered <= 2);
    assert(report.blit_required);
    assert(count_changed_pixels(before, pixels, 16 * 12) > 0);
}

static void test_window_change_forces_full_redraw(void)
{
    uint8_t samples[64];
    memset(samples, 0x1Fu, sizeof(samples));
    ui_waveform_source_t source = {
        .kind = UI_WAVEFORM_SOURCE_HIGH,
        .samples = samples,
        .sample_count = sizeof(samples),
    };
    uint16_t pixels[16 * 12] = {0};
    ui_overview_wave_cache_t cache = {0};
    ui_overview_wave_cache_report_t report;

    ui_overview_wave_cache_reset(&cache);
    assert(ui_overview_wave_cache_bind(&cache, pixels, 16, 16, 12,
                                       palette, sizeof(palette) / sizeof(palette[0])));
    assert(ui_overview_wave_cache_update(&cache, &source, 64000, NULL,
                                         32000, 16000, &report));
    assert(ui_overview_wave_cache_update(&cache, &source, 64000, NULL,
                                         32000, 8000, &report));

    assert(report.kind == UI_OVERVIEW_WAVE_CACHE_FULL);
    assert(report.columns_rendered == 16);
}

static void test_subpixel_advance_accumulates_until_visible_scroll(void)
{
    uint8_t samples[256];
    for (int i = 0; i < 256; i++) {
        samples[i] = (uint8_t)(0x10u + (i & 0x0Fu));
    }
    ui_waveform_source_t source = {
        .kind = UI_WAVEFORM_SOURCE_HIGH,
        .samples = samples,
        .sample_count = sizeof(samples),
    };
    uint16_t pixels[16 * 12] = {0};
    ui_overview_wave_cache_t cache = {0};
    ui_overview_wave_cache_report_t report;

    ui_overview_wave_cache_reset(&cache);
    assert(ui_overview_wave_cache_bind(&cache, pixels, 16, 16, 12,
                                       palette, sizeof(palette) / sizeof(palette[0])));
    assert(ui_overview_wave_cache_update(&cache, &source, 64000, NULL,
                                         32000, 16000, &report));
    assert(!ui_overview_wave_cache_update(&cache, &source, 64000, NULL,
                                          32250, 16000, &report));
    assert(ui_overview_wave_cache_update(&cache, &source, 64000, NULL,
                                         32550, 16000, &report));

    assert(report.kind == UI_OVERVIEW_WAVE_CACHE_SCROLL);
    assert(report.scroll_dx_px == 1);
    assert(report.columns_rendered == 1);
}

static void test_large_jump_forces_full_redraw(void)
{
    uint8_t samples[64];
    memset(samples, 0x1Fu, sizeof(samples));
    ui_waveform_source_t source = {
        .kind = UI_WAVEFORM_SOURCE_HIGH,
        .samples = samples,
        .sample_count = sizeof(samples),
    };
    uint16_t pixels[16 * 12] = {0};
    ui_overview_wave_cache_t cache = {0};
    ui_overview_wave_cache_report_t report;

    ui_overview_wave_cache_reset(&cache);
    assert(ui_overview_wave_cache_bind(&cache, pixels, 16, 16, 12,
                                       palette, sizeof(palette) / sizeof(palette[0])));
    assert(ui_overview_wave_cache_update(&cache, &source, 64000, NULL,
                                         8000, 16000, &report));
    assert(ui_overview_wave_cache_update(&cache, &source, 64000, NULL,
                                         56000, 16000, &report));

    assert(report.kind == UI_OVERVIEW_WAVE_CACHE_FULL);
    assert(report.columns_rendered == 16);
}

static void test_missing_source_returns_false_without_blit(void)
{
    uint16_t pixels[16 * 12] = {0};
    ui_waveform_source_t source = {
        .kind = UI_WAVEFORM_SOURCE_NONE,
        .samples = NULL,
        .sample_count = 0,
    };
    ui_overview_wave_cache_t cache = {0};
    ui_overview_wave_cache_report_t report;

    ui_overview_wave_cache_reset(&cache);
    assert(ui_overview_wave_cache_bind(&cache, pixels, 16, 16, 12,
                                       palette, sizeof(palette) / sizeof(palette[0])));
    assert(!ui_overview_wave_cache_update(&cache, &source, 64000, NULL,
                                          32000, 16000, &report));
    assert(report.kind == UI_OVERVIEW_WAVE_CACHE_NONE);
    assert(!report.blit_required);
}

int main(void)
{
    test_initial_update_renders_full_view();
    test_small_center_advance_scrolls_cached_view();
    test_window_change_forces_full_redraw();
    test_subpixel_advance_accumulates_until_visible_scroll();
    test_large_jump_forces_full_redraw();
    test_missing_source_returns_false_without_blit();
    puts("ui_overview_wave_cache tests passed");
    return 0;
}
