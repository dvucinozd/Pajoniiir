# P4 Overview Waveform Cache Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make dual-deck overview waveforms fluid by avoiding full 648x141 waveform re-rendering on every UI tick.

**Architecture:** Keep P4 authoritative and keep the existing RGB565/PPA direct overlay path. Add a per-deck RGB565 viewport cache that scrolls the previous frame and renders only newly exposed waveform columns for small position advances. Keep full redraws only for load/source/window changes, large seeks, tab re-entry, and cache invalidation.

**Tech Stack:** C, ESP-IDF v5.5, FreeRTOS, LVGL, ESP32-P4 PPA, existing GCC host test runner.

---

## Evidence

Diagnostic capture `C:\Users\Daniel\AppData\Local\Temp\ddj_ffl4_com15_diag_20260615_211300.log` shows:

- No panic, watchdog, backtrace, or reset after `deck 2 play -> PLAYING`.
- `D1/D2 overview main render` averages roughly `11-12 ms`, with max around `15 ms`.
- `D1/D2 overview overlay total` averages roughly `4.8-5.2 ms`.
- `ui_update interval` during dual-deck playback averages roughly `38-40 ms`, with spikes above `70 ms`.

Conclusion: the remaining issue is not the previous shared decode-buffer race. The main bottleneck is the full main waveform render cost on every frame.

## Files

- Create: `firmware/main-deck-p4/components/ui/include/ui_overview_wave_cache.h`
  - Public cache API for host tests and `ui_overview.c`.
- Create: `firmware/main-deck-p4/components/ui/ui_overview_wave_cache.c`
  - Per-viewport RGB565 cache update logic: full rebuild, scroll reuse, exposed-column render, metrics.
- Modify: `firmware/main-deck-p4/components/ui/include/ui_overview_renderer.h`
  - Add column-range RGB565 render helper used by the cache.
- Modify: `firmware/main-deck-p4/components/ui/ui_overview_renderer.c`
  - Extract/reuse column rendering so a small destination column range can be rendered without clearing the whole viewport.
- Modify: `firmware/main-deck-p4/components/ui/ui_overview.c`
  - Replace direct full `ui_overview_renderer_draw_main_rgb565()` calls in the runtime path with `ui_overview_wave_cache_update()`.
- Modify: `firmware/main-deck-p4/components/ui/CMakeLists.txt`
  - Add `ui_overview_wave_cache.c`.
- Create: `tests/ui_overview_wave_cache/test_ui_overview_wave_cache.c`
  - Host tests for full rebuild, small scroll, invalidation, large jump, and source-missing behavior.
- Modify: `tests/run_p4_host_tests.ps1`
  - Add `ui_overview_wave_cache` to the P4 regression runner.
- Modify: `docs/DEVELOPMENT_PLAN.md`
  - Add a short note that overview main waveform uses RGB565 scroll cache instead of full render per UI tick.

## Data Model

Add this header:

```c
// firmware/main-deck-p4/components/ui/include/ui_overview_wave_cache.h
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rekordbox_anlz.h"
#include "ui_waveform_model.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_OVERVIEW_WAVE_CACHE_NONE = 0,
    UI_OVERVIEW_WAVE_CACHE_FULL,
    UI_OVERVIEW_WAVE_CACHE_SCROLL,
} ui_overview_wave_cache_update_kind_t;

typedef struct {
    bool valid;
    const uint8_t *source_samples;
    uint32_t source_sample_count;
    ui_waveform_source_kind_t source_kind;
    uint32_t duration_ms;
    uint32_t center_ms;
    uint32_t window_ms;
    const anlz_metadata_t *meta;
    uint16_t *pixels;
    int stride_px;
    int width_px;
    int height_px;
    const uint16_t *palette;
    size_t palette_count;
} ui_overview_wave_cache_t;

typedef struct {
    ui_overview_wave_cache_update_kind_t kind;
    int scroll_dx_px;
    uint16_t columns_rendered;
    bool blit_required;
} ui_overview_wave_cache_report_t;

void ui_overview_wave_cache_reset(ui_overview_wave_cache_t *cache);

bool ui_overview_wave_cache_bind(ui_overview_wave_cache_t *cache,
                                 uint16_t *pixels,
                                 int stride_px,
                                 int width_px,
                                 int height_px,
                                 const uint16_t *palette,
                                 size_t palette_count);

bool ui_overview_wave_cache_update(ui_overview_wave_cache_t *cache,
                                   const ui_waveform_source_t *source,
                                   uint32_t duration_ms,
                                   const anlz_metadata_t *meta,
                                   uint32_t center_ms,
                                   uint32_t window_ms,
                                   ui_overview_wave_cache_report_t *out_report);

#ifdef __cplusplus
}
#endif
```

## Task 1: Add Cache Host Test Skeleton

**Files:**
- Create: `tests/ui_overview_wave_cache/test_ui_overview_wave_cache.c`
- Modify: `tests/run_p4_host_tests.ps1`

- [ ] **Step 1: Create the failing cache test file**

Create `tests/ui_overview_wave_cache/test_ui_overview_wave_cache.c`:

```c
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
        if (a[i] != b[i]) changed++;
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
    ui_overview_wave_cache_t cache;
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
    ui_overview_wave_cache_t cache;
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
    ui_overview_wave_cache_t cache;
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
    ui_overview_wave_cache_t cache;
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
    ui_overview_wave_cache_t cache;
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
    test_large_jump_forces_full_redraw();
    test_missing_source_returns_false_without_blit();
    puts("ui_overview_wave_cache tests passed");
    return 0;
}
```

- [ ] **Step 2: Add the new test to the host runner**

Add this entry to `$tests` in `tests/run_p4_host_tests.ps1` after `ui_overview_renderer`:

```powershell
    @{
        Name = "ui_overview_wave_cache"
        Dir = "tests/ui_overview_wave_cache"
        Target = "test_ui_overview_wave_cache.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-Werror=implicit-function-declaration", "-std=c99",
            "-DANLZ_STANDALONE_TEST",
            "-I../../firmware/main-deck-p4/components/ui/include",
            "-I../../firmware/main-deck-p4/components/library/include",
            "-o", "test_ui_overview_wave_cache.exe",
            "test_ui_overview_wave_cache.c",
            "../../firmware/main-deck-p4/components/ui/ui_overview_wave_cache.c",
            "../../firmware/main-deck-p4/components/ui/ui_overview_renderer.c",
            "../../firmware/main-deck-p4/components/ui/ui_waveform_model.c",
            "../../firmware/main-deck-p4/components/ui/ui_overview_grid.c"
        )
    },
```

- [ ] **Step 3: Run RED**

Run:

```powershell
.\tests\run_p4_host_tests.ps1
```

Expected: FAIL while building `ui_overview_wave_cache` because `ui_overview_wave_cache.h` / `.c` do not exist yet.

- [ ] **Step 4: Commit test skeleton**

Do not commit yet if RED is the only state. Keep it as part of Task 2 commit after GREEN.

## Task 2: Add Column-Range Renderer

**Files:**
- Modify: `firmware/main-deck-p4/components/ui/include/ui_overview_renderer.h`
- Modify: `firmware/main-deck-p4/components/ui/ui_overview_renderer.c`
- Modify: `tests/ui_overview_renderer/test_ui_overview_renderer.c`

- [ ] **Step 1: Add failing renderer test**

Append to `tests/ui_overview_renderer/test_ui_overview_renderer.c` before `main()`:

```c
static void test_main_rgb565_renderer_can_draw_column_range_without_clearing_all(void)
{
    uint8_t samples[16];
    memset(samples, 0x1Fu, sizeof(samples));
    ui_waveform_source_t source = {
        .kind = UI_WAVEFORM_SOURCE_LOW,
        .samples = samples,
        .sample_count = sizeof(samples),
    };
    const uint16_t palette[] = {
        0x0000, 0xF16E, 0x235F, 0x475C, 0xE71D,
        0x1F32, 0xFD66, 0x9ADF, 0x3989,
    };
    uint16_t pixels[8 * 10];
    for (size_t i = 0; i < sizeof(pixels) / sizeof(pixels[0]); i++) {
        pixels[i] = 0xAAAA;
    }

    ui_overview_renderer_draw_main_rgb565_columns(pixels, 8, 8, 10,
                                                  6, 2,
                                                  &source, 8000, NULL,
                                                  4000, 8000, palette,
                                                  sizeof(palette) / sizeof(palette[0]));

    assert(pixels[0] == 0xAAAA);
    assert(pixels[(10 / 2) * 8 + 6] != 0xAAAA);
    assert(pixels[(10 / 2) * 8 + 7] != 0xAAAA);
}
```

Call it from `main()`:

```c
    test_main_rgb565_renderer_can_draw_column_range_without_clearing_all();
```

- [ ] **Step 2: Run RED**

Run:

```powershell
Push-Location tests\ui_overview_renderer
gcc -Wall -Wextra -Wpedantic -Werror=implicit-function-declaration -std=c99 `
    -DANLZ_STANDALONE_TEST `
    -I../../firmware/main-deck-p4/components/ui/include `
    -I../../firmware/main-deck-p4/components/library/include `
    -o test_ui_overview_renderer.exe `
    test_ui_overview_renderer.c `
    ../../firmware/main-deck-p4/components/ui/ui_overview_renderer.c `
    ../../firmware/main-deck-p4/components/ui/ui_waveform_model.c `
    ../../firmware/main-deck-p4/components/ui/ui_overview_grid.c
Pop-Location
```

Expected: FAIL with implicit declaration for `ui_overview_renderer_draw_main_rgb565_columns`.

- [ ] **Step 3: Add renderer API**

Add to `firmware/main-deck-p4/components/ui/include/ui_overview_renderer.h`:

```c
void ui_overview_renderer_draw_main_rgb565_columns(uint16_t *pixels,
                                                   int stride_px,
                                                   int width_px,
                                                   int height_px,
                                                   int dest_x,
                                                   int column_count,
                                                   const ui_waveform_source_t *source,
                                                   uint32_t duration_ms,
                                                   const anlz_metadata_t *meta,
                                                   uint32_t center_ms,
                                                   uint32_t window_ms,
                                                   const uint16_t *palette,
                                                   size_t palette_count);
```

- [ ] **Step 4: Implement the column-range helper**

In `ui_overview_renderer.c`, add this helper above `ui_overview_renderer_draw_main_rgb565()`:

```c
static void clear_rgb565_columns(uint16_t *pixels,
                                 int stride_px,
                                 int height_px,
                                 int dest_x,
                                 int column_count)
{
    for (int y = 0; y < height_px; y++) {
        memset(&pixels[y * stride_px + dest_x], 0,
               (size_t)column_count * sizeof(uint16_t));
    }
}

void ui_overview_renderer_draw_main_rgb565_columns(uint16_t *pixels,
                                                   int stride_px,
                                                   int width_px,
                                                   int height_px,
                                                   int dest_x,
                                                   int column_count,
                                                   const ui_waveform_source_t *source,
                                                   uint32_t duration_ms,
                                                   const anlz_metadata_t *meta,
                                                   uint32_t center_ms,
                                                   uint32_t window_ms,
                                                   const uint16_t *palette,
                                                   size_t palette_count)
{
    if (!pixels || stride_px <= 0 || width_px <= 0 || height_px <= 0 ||
        !source || !source->samples || source->sample_count == 0 ||
        duration_ms == 0 || window_ms == 0 || dest_x >= width_px ||
        column_count <= 0) {
        return;
    }
    if (dest_x < 0) {
        column_count += dest_x;
        dest_x = 0;
    }
    if (column_count <= 0) {
        return;
    }
    if (dest_x + column_count > width_px) {
        column_count = width_px - dest_x;
    }

    clear_rgb565_columns(pixels, stride_px, height_px, dest_x, column_count);

    int64_t window_start = (int64_t)center_ms - (int64_t)(window_ms / 2u);
    for (int x = dest_x; x < dest_x + column_count; x++) {
        ui_waveform_column_t col = main_waveform_column_for_display(
            source, duration_ms, window_start, window_ms, x, width_px);
        uint16_t color = rgb565_palette_color(palette, palette_count,
                                               col.palette_index);
        if (col.peak == 0 || color == 0) {
            continue;
        }

        int peak_h = (col.peak * (height_px - 2)) / 31;
        if (peak_h < 1) peak_h = 1;
        int y0 = (height_px / 2) - peak_h / 2;
        int y1 = y0 + peak_h;
        if (y0 < 0) y0 = 0;
        if (y1 > height_px) y1 = height_px;
        for (int y = y0; y < y1; y++) {
            pixels[y * stride_px + x] = color;
        }
    }

    draw_zoom_grid_rgb565(pixels, stride_px, width_px, height_px,
                          (int64_t)center_ms - (int64_t)(window_ms / 2u),
                          window_ms, meta, palette, palette_count);
    draw_center_playhead_rgb565(pixels, stride_px, width_px, height_px,
                                palette, palette_count);
}
```

- [ ] **Step 5: Refactor full RGB565 renderer to use the helper**

Replace the body of `ui_overview_renderer_draw_main_rgb565()` after argument checks with:

```c
    memset(pixels, 0, (size_t)stride_px * (size_t)height_px * sizeof(uint16_t));
    ui_overview_renderer_draw_main_rgb565_columns(pixels,
                                                  stride_px,
                                                  width_px,
                                                  height_px,
                                                  0,
                                                  width_px,
                                                  source,
                                                  duration_ms,
                                                  meta,
                                                  center_ms,
                                                  window_ms,
                                                  palette,
                                                  palette_count);
```

- [ ] **Step 6: Run GREEN**

Run:

```powershell
.\tests\run_p4_host_tests.ps1
```

Expected: all host tests PASS.

## Task 3: Implement RGB565 Viewport Scroll Cache

**Files:**
- Create: `firmware/main-deck-p4/components/ui/include/ui_overview_wave_cache.h`
- Create: `firmware/main-deck-p4/components/ui/ui_overview_wave_cache.c`
- Modify: `firmware/main-deck-p4/components/ui/CMakeLists.txt`

- [ ] **Step 1: Add header exactly as defined in Data Model**

Create `firmware/main-deck-p4/components/ui/include/ui_overview_wave_cache.h` using the header from the Data Model section.

- [ ] **Step 2: Add cache implementation**

Create `firmware/main-deck-p4/components/ui/ui_overview_wave_cache.c`:

```c
#include "ui_overview_wave_cache.h"

#include <stdlib.h>
#include <string.h>

#include "ui_overview_renderer.h"

static void report_reset(ui_overview_wave_cache_report_t *report)
{
    if (!report) return;
    *report = (ui_overview_wave_cache_report_t) {
        .kind = UI_OVERVIEW_WAVE_CACHE_NONE,
        .scroll_dx_px = 0,
        .columns_rendered = 0,
        .blit_required = false,
    };
}

void ui_overview_wave_cache_reset(ui_overview_wave_cache_t *cache)
{
    if (!cache) return;
    uint16_t *pixels = cache->pixels;
    int stride = cache->stride_px;
    int width = cache->width_px;
    int height = cache->height_px;
    const uint16_t *palette = cache->palette;
    size_t palette_count = cache->palette_count;
    memset(cache, 0, sizeof(*cache));
    cache->pixels = pixels;
    cache->stride_px = stride;
    cache->width_px = width;
    cache->height_px = height;
    cache->palette = palette;
    cache->palette_count = palette_count;
}

bool ui_overview_wave_cache_bind(ui_overview_wave_cache_t *cache,
                                 uint16_t *pixels,
                                 int stride_px,
                                 int width_px,
                                 int height_px,
                                 const uint16_t *palette,
                                 size_t palette_count)
{
    if (!cache || !pixels || stride_px < width_px || width_px <= 0 ||
        height_px <= 0 || !palette || palette_count == 0) {
        return false;
    }
    memset(cache, 0, sizeof(*cache));
    cache->pixels = pixels;
    cache->stride_px = stride_px;
    cache->width_px = width_px;
    cache->height_px = height_px;
    cache->palette = palette;
    cache->palette_count = palette_count;
    return true;
}

static bool source_matches(const ui_overview_wave_cache_t *cache,
                           const ui_waveform_source_t *source,
                           uint32_t duration_ms,
                           const anlz_metadata_t *meta,
                           uint32_t window_ms)
{
    return cache && cache->valid && source &&
           cache->source_samples == source->samples &&
           cache->source_sample_count == source->sample_count &&
           cache->source_kind == source->kind &&
           cache->duration_ms == duration_ms &&
           cache->meta == meta &&
           cache->window_ms == window_ms;
}

static void cache_store_key(ui_overview_wave_cache_t *cache,
                            const ui_waveform_source_t *source,
                            uint32_t duration_ms,
                            const anlz_metadata_t *meta,
                            uint32_t center_ms,
                            uint32_t window_ms)
{
    cache->source_samples = source->samples;
    cache->source_sample_count = source->sample_count;
    cache->source_kind = source->kind;
    cache->duration_ms = duration_ms;
    cache->meta = meta;
    cache->center_ms = center_ms;
    cache->window_ms = window_ms;
    cache->valid = true;
}

static int center_delta_to_pixels(uint32_t old_center_ms,
                                  uint32_t new_center_ms,
                                  uint32_t window_ms,
                                  int width_px)
{
    int64_t delta_ms = (int64_t)new_center_ms - (int64_t)old_center_ms;
    int64_t numerator = delta_ms * (int64_t)width_px;
    if (numerator >= 0) {
        return (int)((numerator + (int64_t)(window_ms / 2u)) / (int64_t)window_ms);
    }
    return (int)((numerator - (int64_t)(window_ms / 2u)) / (int64_t)window_ms);
}

static void scroll_pixels(uint16_t *pixels,
                          int stride_px,
                          int width_px,
                          int height_px,
                          int dx)
{
    if (dx == 0) return;
    for (int y = 0; y < height_px; y++) {
        uint16_t *row = &pixels[y * stride_px];
        if (dx > 0) {
            memmove(row, row + dx, (size_t)(width_px - dx) * sizeof(uint16_t));
            memset(row + width_px - dx, 0, (size_t)dx * sizeof(uint16_t));
        } else {
            int left = -dx;
            memmove(row + left, row, (size_t)(width_px - left) * sizeof(uint16_t));
            memset(row, 0, (size_t)left * sizeof(uint16_t));
        }
    }
}

bool ui_overview_wave_cache_update(ui_overview_wave_cache_t *cache,
                                   const ui_waveform_source_t *source,
                                   uint32_t duration_ms,
                                   const anlz_metadata_t *meta,
                                   uint32_t center_ms,
                                   uint32_t window_ms,
                                   ui_overview_wave_cache_report_t *out_report)
{
    report_reset(out_report);
    if (!cache || !cache->pixels || !source || !source->samples ||
        source->sample_count == 0 || source->kind == UI_WAVEFORM_SOURCE_NONE ||
        duration_ms == 0 || window_ms == 0) {
        return false;
    }

    bool full = !source_matches(cache, source, duration_ms, meta, window_ms);
    int dx = 0;
    if (!full) {
        dx = center_delta_to_pixels(cache->center_ms, center_ms,
                                    window_ms, cache->width_px);
        if (dx == 0) {
            cache->center_ms = center_ms;
            return false;
        }
        if (abs(dx) >= cache->width_px) {
            full = true;
        }
    }

    ui_overview_wave_cache_report_t report = {0};
    if (full) {
        ui_overview_renderer_draw_main_rgb565(cache->pixels,
                                              cache->stride_px,
                                              cache->width_px,
                                              cache->height_px,
                                              source,
                                              duration_ms,
                                              meta,
                                              center_ms,
                                              window_ms,
                                              cache->palette,
                                              cache->palette_count);
        report.kind = UI_OVERVIEW_WAVE_CACHE_FULL;
        report.scroll_dx_px = 0;
        report.columns_rendered = (uint16_t)cache->width_px;
        report.blit_required = true;
    } else {
        scroll_pixels(cache->pixels, cache->stride_px,
                      cache->width_px, cache->height_px, dx);
        if (dx > 0) {
            ui_overview_renderer_draw_main_rgb565_columns(cache->pixels,
                                                          cache->stride_px,
                                                          cache->width_px,
                                                          cache->height_px,
                                                          cache->width_px - dx,
                                                          dx,
                                                          source,
                                                          duration_ms,
                                                          meta,
                                                          center_ms,
                                                          window_ms,
                                                          cache->palette,
                                                          cache->palette_count);
            report.columns_rendered = (uint16_t)dx;
        } else {
            ui_overview_renderer_draw_main_rgb565_columns(cache->pixels,
                                                          cache->stride_px,
                                                          cache->width_px,
                                                          cache->height_px,
                                                          0,
                                                          -dx,
                                                          source,
                                                          duration_ms,
                                                          meta,
                                                          center_ms,
                                                          window_ms,
                                                          cache->palette,
                                                          cache->palette_count);
            report.columns_rendered = (uint16_t)(-dx);
        }
        report.kind = UI_OVERVIEW_WAVE_CACHE_SCROLL;
        report.scroll_dx_px = dx;
        report.blit_required = true;
    }

    cache_store_key(cache, source, duration_ms, meta, center_ms, window_ms);
    if (out_report) {
        *out_report = report;
    }
    return true;
}
```

- [ ] **Step 3: Add file to UI component**

In `firmware/main-deck-p4/components/ui/CMakeLists.txt`, add:

```cmake
         "ui_overview_wave_cache.c"
```

near `ui_overview_renderer.c`.

- [ ] **Step 4: Run GREEN for cache tests**

Run:

```powershell
.\tests\run_p4_host_tests.ps1
```

Expected: all host tests PASS, including `ui_overview_wave_cache`.

- [ ] **Step 5: Commit**

```powershell
git add firmware/main-deck-p4/components/ui/include/ui_overview_renderer.h `
        firmware/main-deck-p4/components/ui/ui_overview_renderer.c `
        firmware/main-deck-p4/components/ui/include/ui_overview_wave_cache.h `
        firmware/main-deck-p4/components/ui/ui_overview_wave_cache.c `
        firmware/main-deck-p4/components/ui/CMakeLists.txt `
        tests/ui_overview_renderer/test_ui_overview_renderer.c `
        tests/ui_overview_wave_cache/test_ui_overview_wave_cache.c `
        tests/run_p4_host_tests.ps1
git commit -m "perf(ui): add overview waveform scroll cache"
```

## Task 4: Integrate Cache Into Overview Runtime

**Files:**
- Modify: `firmware/main-deck-p4/components/ui/ui_overview.c`
- Modify: `tests/run_p4_host_tests.ps1`

- [ ] **Step 1: Add cache fields in `ui_overview.c`**

Add include:

```c
#include "ui_overview_wave_cache.h"
```

Add static cache storage under existing overlay buffers:

```c
static ui_overview_wave_cache_t s_overview_wave_cache[DECK_CORE_DECK_COUNT];
```

- [ ] **Step 2: Bind cache after overlay buffer allocation**

In `ui_overview_wave_overlay_ensure_buffer()`, after allocating/clearing `s_overview_wave_overlay_rgb565[idx]`, add:

```c
    ui_overview_wave_cache_bind(&s_overview_wave_cache[idx],
                                s_overview_wave_overlay_rgb565[idx],
                                OVERVIEW_CV_W,
                                OVERVIEW_CV_W,
                                OVERVIEW_CV_H,
                                s_overview_wave_rgb565_palette,
                                sizeof(s_overview_wave_rgb565_palette) /
                                    sizeof(s_overview_wave_rgb565_palette[0]));
```

- [ ] **Step 3: Reset cache on load/source changes**

In `ui_overview_load_waveform_data()`, after resetting `panel->last_wave_center_ms`, add:

```c
#ifndef WIN32
    ui_overview_wave_cache_reset(&s_overview_wave_cache[idx]);
#endif
```

- [ ] **Step 4: Replace full runtime render with cache update**

In `ui_render_overview_main_waveform()` under `#ifndef WIN32`, replace:

```c
        uint16_t *overlay = s_overview_wave_overlay_rgb565[idx];
        int64_t render_start_us = ui_diagnostics_enabled() ? esp_timer_get_time() : 0;
        ui_overview_renderer_draw_main_rgb565(overlay, W, W, H, source,
                                              duration_ms, meta, center_ms,
                                              window_ms,
                                              s_overview_wave_rgb565_palette,
                                              sizeof(s_overview_wave_rgb565_palette) /
                                                  sizeof(s_overview_wave_rgb565_palette[0]));
```

with:

```c
        uint16_t *overlay = s_overview_wave_overlay_rgb565[idx];
        ui_overview_wave_cache_report_t cache_report = {0};
        int64_t render_start_us = ui_diagnostics_enabled() ? esp_timer_get_time() : 0;
        bool cache_updated = ui_overview_wave_cache_update(&s_overview_wave_cache[idx],
                                                           source,
                                                           duration_ms,
                                                           meta,
                                                           center_ms,
                                                           window_ms,
                                                           &cache_report);
        if (!cache_updated || !cache_report.blit_required) {
            return;
        }
```

Keep existing diagnostic render timing block, but change its log label to include update kind:

```c
                         "D%u overview main cache: kind=%u dx=%d cols=%u last=%u us avg=%u us max=%u us samples=%u",
                         (unsigned)(idx + 1u),
                         (unsigned)cache_report.kind,
                         cache_report.scroll_dx_px,
                         (unsigned)cache_report.columns_rendered,
```

- [ ] **Step 5: Add static guard against full RGB565 draw in runtime file**

Add to `tests/run_p4_host_tests.ps1`:

```powershell
Assert-FileDoesNotContain `
    -Name "overview runtime avoids full RGB565 redraw" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/ui/ui_overview.c") `
    -Patterns @("ui_overview_renderer_draw_main_rgb565(overlay")
```

- [ ] **Step 6: Run host tests**

Run:

```powershell
.\tests\run_p4_host_tests.ps1
```

Expected: all tests PASS.

- [ ] **Step 7: Run P4 build**

Run:

```powershell
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1
cd D:\Documents\DDJ-FFL4\firmware\main-deck-p4
idf.py build
```

Expected: build PASS.

- [ ] **Step 8: Commit**

```powershell
git add firmware/main-deck-p4/components/ui/ui_overview.c tests/run_p4_host_tests.ps1
git commit -m "perf(ui): use scroll cache for overview waveforms"
```

## Task 5: Hardware Diagnostic Verification

**Files:**
- No source changes expected.

- [ ] **Step 1: Flash normal firmware**

Run:

```powershell
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1
cd D:\Documents\DDJ-FFL4\firmware\main-deck-p4
idf.py -p COM15 flash
```

Expected: flash PASS and hard reset.

- [ ] **Step 2: Manual smoke test**

On hardware:

1. Load track on deck 1.
2. Load track on deck 2.
3. Start deck 1 and watch main waveform for 10 seconds.
4. Start deck 2 and watch both main waveforms for 30 seconds.
5. Confirm there is no reboot, no hard UI freeze, and no obvious second-deck onset stall.

- [ ] **Step 3: Build diagnostic firmware without source edits**

Run:

```powershell
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1
cd D:\Documents\DDJ-FFL4\firmware\main-deck-p4
idf.py -D CMAKE_C_FLAGS="-DUI_DIAGNOSTICS_ENABLED=1" reconfigure build
idf.py -p COM15 flash
```

Expected: diagnostic build and flash PASS.

- [ ] **Step 4: Capture diagnostic log**

Run:

```powershell
$port = [System.IO.Ports.SerialPort]::new('COM15',115200,[System.IO.Ports.Parity]::None,8,[System.IO.Ports.StopBits]::One)
$port.ReadTimeout = 200
$port.DtrEnable = $false
$port.RtsEnable = $false
$deadline = (Get-Date).AddSeconds(120)
$buf = New-Object System.Text.StringBuilder
try {
    $port.Open()
    [void]$buf.AppendLine('--- COM15 waveform cache diagnostic capture start ---')
    while ((Get-Date) -lt $deadline) {
        $s = $port.ReadExisting()
        if ($s.Length -gt 0) { [void]$buf.Append($s) }
        Start-Sleep -Milliseconds 80
    }
    [void]$buf.AppendLine('--- COM15 waveform cache diagnostic capture end ---')
} finally {
    if ($port.IsOpen) { $port.Close() }
}
$path = Join-Path $env:TEMP ('ddj_ffl4_wave_cache_' + (Get-Date -Format 'yyyyMMdd_HHmmss') + '.log')
Set-Content -Path $path -Value $buf.ToString() -Encoding UTF8
Write-Output $path
```

During capture, repeat the dual-deck smoke test.

- [ ] **Step 5: Validate expected diagnostic numbers**

Search:

```powershell
Select-String -Path $path -Pattern 'overview main cache|overview overlay total|ui_update duration|ui_update interval|LVGL handler interval|spike window max'
```

Expected targets:

- `overview main cache` average below `3000 us` in steady scrolling.
- `overview overlay total` still around `4000-5500 us`; this is expected until a later PPA/flush optimization.
- `ui_update interval` during dual-deck playback should average close to `16-24 ms` on diagnostic firmware, or at least materially below the previous `38-40 ms`.
- No panic, watchdog, backtrace, or reset.

- [ ] **Step 6: Restore normal firmware**

Run:

```powershell
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1
cd D:\Documents\DDJ-FFL4\firmware\main-deck-p4
idf.py -D "CMAKE_C_FLAGS=" reconfigure build
idf.py -p COM15 flash
```

Expected: normal build and flash PASS.

## Task 6: Documentation and Final Checks

**Files:**
- Modify: `docs/DEVELOPMENT_PLAN.md`

- [ ] **Step 1: Update development plan**

Add a short bullet to the UI/performance section:

```markdown
- Overview main waveform runtime rendering now uses a per-deck RGB565 viewport
  scroll cache. The cache performs full redraw only on load/source/window/seek
  invalidation and renders only newly exposed columns during steady playback.
  This removes the measured 11-12 ms per-frame full waveform render cost from
  the dual-deck steady-state path; PPA overlay blit remains the next measured
  optimization target.
```

- [ ] **Step 2: Run final host tests**

Run:

```powershell
.\tests\run_p4_host_tests.ps1
```

Expected: `P4 host tests passed.`

- [ ] **Step 3: Run P4 build**

Run:

```powershell
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1
cd D:\Documents\DDJ-FFL4\firmware\main-deck-p4
idf.py build
```

Expected: build PASS.

- [ ] **Step 4: Run diff checks**

Run:

```powershell
git diff --check
git status --short --untracked-files=all
```

Expected:

- `git diff --check` exit code `0`.
- No staged or unstaged `build/`, `sdkconfig`, `.exe`, `.log`, `.tmp`, or `.codex-remote-attachments/` artifacts.

- [ ] **Step 5: Commit docs and final checks**

```powershell
git add docs/DEVELOPMENT_PLAN.md
git commit -m "docs: document overview waveform cache"
```

## Acceptance Criteria

- No full main waveform RGB565 redraw in steady `ui_overview.c` runtime path.
- Small playback advances reuse cached RGB565 pixels and render only newly exposed columns.
- Full redraw still happens on load/source/window changes and large seeks.
- Dual-deck playback no longer creates `ui_update interval` averages around `38-40 ms` from full render workload.
- No reboot, panic, watchdog, or backtrace during dual-deck hardware smoke test.
- Host runner includes a regression test for the cache behavior.
- P4 build passes.
- Diagnostic firmware is restored to normal non-diagnostic firmware after measurements.

## Next Optimization After This Plan

If dual-deck still has visible stutter after cache integration, the next measured bottleneck is not waveform rendering but the display path:

- `overview overlay total` remains around `4-5 ms` per blit.
- LVGL render/refresh still has occasional spikes.

The next plan should investigate PPA/LVGL framebuffer handoff, avoiding redundant LVGL invalidation around overlay regions, and possibly moving overlay blit to a non-blocking or refresh-synchronized path.

