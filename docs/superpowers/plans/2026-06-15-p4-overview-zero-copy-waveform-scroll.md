# P4 Overview Zero-Copy Waveform Scroll Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

## Execution Status

- Implementation tasks 1-6 are complete and committed on
  `codex/p4-overview-zero-copy-waveform-scroll`.
- Task 7 firmware build and COM15 flash passed on 2026-06-16. A diagnostics
  COM15 capture with both decks playing showed steady `OFFSET` cache updates at
  roughly 10 us per deck, bounded `EDGE` updates at roughly 0.5 ms for 32
  columns, and no panic/watchdog/brownout. The remaining runtime cost is the
  per-deck PPA overlay copy plus LVGL render/refr spikes, not cache scrolling.
- Task 8 documentation was updated to describe the circular RGB565 strip model
  and the remaining dual-deck capture step.
- Final verification should run the P4 host test runner, P4 build,
  `git diff --check`, and artifact status checks before closing the branch.

**Goal:** Make dual-deck overview waveform playback fluid by removing the steady-state CPU scroll/memmove of full RGB565 waveform buffers. The main waveform path must reuse an already-rendered RGB565 strip and move the visible viewport by PPA source-offset blits, rendering only small edge batches when new waveform columns enter the strip.

**Architecture:** P4 keeps one wider RGB565 strip per deck. The strip is a circular cache in waveform-column space. The UI update path computes a visible source window inside that strip and blits one or two source segments directly to the panel using a new PPA source-region API. Steady playback does not mutate the RGB565 strip at all; occasional edge advance renders only newly exposed columns. Full strip rebuilds happen only on load, source change, zoom/window change, missing ANLZ data, or large seeks.

**Tech Stack:** C, ESP-IDF v5.5, FreeRTOS, LVGL, ESP PPA 270-degree blits, existing PC GCC host tests, PowerShell host-test runner.

---

## Current Problem

The current optimization already removed the old full renderer call from the overview update loop, but it still physically scrolls each deck's visible `648x141` RGB565 buffer with CPU `memmove()` before drawing the new edge columns. Hardware logs show the expected symptom:

- One active deck is mostly fluid.
- Two active decks push the UI interval back into visible jank.
- `overview main cache` remains around 5-7 ms even after RGB565 direct rendering and DMA allocation changes.

That means the remaining CPU cost is dominated by memory movement of large RGB565 rectangles and not by I8-to-RGB conversion. The correct fix is to stop moving pixels for steady scroll.

## Target Runtime Behavior

- Steady playback:
  - no full visible waveform redraw
  - no CPU `memmove()`
  - no full strip rebuild
  - waveform cache update only computes source offsets and returns blit segments
  - PPA copies the visible source region from RGB565 strip to the display

- Edge advance:
  - render a fixed small batch of new columns, for example 24 or 32 columns
  - update circular strip metadata
  - blit visible window from one or two source segments

- Large discontinuity:
  - rebuild full strip
  - reset view origin to the strip center

## File Structure

Modify these files:

- `D:\Documents\DDJ-FFL4\firmware\main-deck-p4\components\ui\include\ui_lvgl_backend.h`
- `D:\Documents\DDJ-FFL4\firmware\main-deck-p4\components\ui\ui_lvgl_backend.c`
- `D:\Documents\DDJ-FFL4\firmware\main-deck-p4\components\ui\include\ui_overview_renderer.h`
- `D:\Documents\DDJ-FFL4\firmware\main-deck-p4\components\ui\ui_overview_renderer.c`
- `D:\Documents\DDJ-FFL4\firmware\main-deck-p4\components\ui\include\ui_overview_wave_cache.h`
- `D:\Documents\DDJ-FFL4\firmware\main-deck-p4\components\ui\ui_overview_wave_cache.c`
- `D:\Documents\DDJ-FFL4\firmware\main-deck-p4\components\ui\ui_overview.c`
- `D:\Documents\DDJ-FFL4\tests\ui_overview_wave_cache\test_ui_overview_wave_cache.c`
- `D:\Documents\DDJ-FFL4\tests\ui_overview_renderer\test_ui_overview_renderer.c`
- `D:\Documents\DDJ-FFL4\tests\run_p4_host_tests.ps1`
- `D:\Documents\DDJ-FFL4\docs\DEVELOPMENT_PLAN.md`

Do not modify:

- UART `0xA5` protocol
- S3 firmware
- audio engine ownership model
- generated `build/`, `managed_components/`, `sdkconfig`, `dependencies.lock`

## Implementation Tasks

### 1. Create a Focused Branch and Record the Baseline

- [ ] Start from the current waveform-cache branch or latest pushed equivalent:

```powershell
cd D:\Documents\DDJ-FFL4
git status --short --untracked-files=all
git switch -c codex/p4-overview-zero-copy-waveform-scroll
```

- [ ] Confirm the current branch still contains the existing cache work:

```powershell
git log --oneline --decorate -5
rg "ui_overview_wave_cache_update" firmware\main-deck-p4\components\ui
rg "overview main cache" firmware\main-deck-p4\components\ui
```

- [ ] Expected result:

```text
ui_overview_wave_cache_update appears in ui_overview.c
overview main cache diagnostics appear in ui_overview.c
```

### 2. Add Failing Host Tests for Zero-Copy Scroll Semantics

- [ ] Extend `tests\ui_overview_wave_cache\test_ui_overview_wave_cache.c` before changing implementation.

Add tests that force the desired behavior:

```c
static void test_steady_advance_uses_offset_without_mutating_pixels(void)
{
    ui_overview_wave_cache_t cache;
    ui_overview_wave_cache_report_t report;
    uint16_t pixels[TEST_STRIP_W * TEST_H];
    uint16_t before[TEST_STRIP_W * TEST_H];

    memset(&cache, 0, sizeof(cache));
    fill_test_source();

    TEST_ASSERT_TRUE(ui_overview_wave_cache_bind_strip(&cache,
                                                       pixels,
                                                       TEST_STRIP_W,
                                                       TEST_STRIP_W,
                                                       TEST_VIEW_W,
                                                       TEST_H,
                                                       TEST_MARGIN_W,
                                                       s_palette,
                                                       ARRAY_SIZE(s_palette)));

    TEST_ASSERT_TRUE(ui_overview_wave_cache_update(&cache, &s_source, 10000, 8000, &report));
    TEST_ASSERT_EQUAL(UI_OVERVIEW_WAVE_CACHE_FULL, report.kind);
    memcpy(before, pixels, sizeof(pixels));

    TEST_ASSERT_TRUE(ui_overview_wave_cache_update(&cache, &s_source, 10080, 8000, &report));
    TEST_ASSERT_EQUAL(UI_OVERVIEW_WAVE_CACHE_OFFSET, report.kind);
    TEST_ASSERT_EQUAL_UINT16(0, report.columns_rendered);
    TEST_ASSERT_TRUE(report.blit_required);
    TEST_ASSERT_TRUE(report.blit_count >= 1);
    TEST_ASSERT_NOT_EQUAL(report.blit[0].src_x_px, TEST_MARGIN_W);
    TEST_ASSERT_EQUAL_MEMORY(before, pixels, sizeof(pixels));
}
```

```c
static void test_edge_advance_renders_small_batch_without_full_rebuild(void)
{
    ui_overview_wave_cache_t cache;
    ui_overview_wave_cache_report_t report;
    uint16_t pixels[TEST_STRIP_W * TEST_H];

    memset(&cache, 0, sizeof(cache));
    TEST_ASSERT_TRUE(ui_overview_wave_cache_bind_strip(&cache,
                                                       pixels,
                                                       TEST_STRIP_W,
                                                       TEST_STRIP_W,
                                                       TEST_VIEW_W,
                                                       TEST_H,
                                                       TEST_MARGIN_W,
                                                       s_palette,
                                                       ARRAY_SIZE(s_palette)));

    TEST_ASSERT_TRUE(ui_overview_wave_cache_update(&cache, &s_source, 10000, 8000, &report));
    TEST_ASSERT_EQUAL(UI_OVERVIEW_WAVE_CACHE_FULL, report.kind);

    TEST_ASSERT_TRUE(ui_overview_wave_cache_update(&cache, &s_source, 10000 + TEST_EDGE_TRIGGER_MS, 8000, &report));
    TEST_ASSERT_EQUAL(UI_OVERVIEW_WAVE_CACHE_EDGE, report.kind);
    TEST_ASSERT_TRUE(report.columns_rendered > 0);
    TEST_ASSERT_TRUE(report.columns_rendered <= UI_OVERVIEW_WAVE_CACHE_EDGE_BATCH_PX);
    TEST_ASSERT_TRUE(report.blit_count >= 1);
}
```

```c
static void test_wrap_reports_two_blit_segments(void)
{
    ui_overview_wave_cache_t cache;
    ui_overview_wave_cache_report_t report;
    uint16_t pixels[TEST_STRIP_W * TEST_H];

    memset(&cache, 0, sizeof(cache));
    TEST_ASSERT_TRUE(ui_overview_wave_cache_bind_strip(&cache,
                                                       pixels,
                                                       TEST_STRIP_W,
                                                       TEST_STRIP_W,
                                                       TEST_VIEW_W,
                                                       TEST_H,
                                                       TEST_MARGIN_W,
                                                       s_palette,
                                                       ARRAY_SIZE(s_palette)));

    TEST_ASSERT_TRUE(ui_overview_wave_cache_update(&cache, &s_source, 10000, 8000, &report));
    force_test_view_origin_near_ring_end(&cache);

    TEST_ASSERT_TRUE(ui_overview_wave_cache_update(&cache, &s_source, 10016, 8000, &report));
    TEST_ASSERT_EQUAL_UINT8(2, report.blit_count);
    TEST_ASSERT_EQUAL_UINT16(TEST_VIEW_W, report.blit[0].width_px + report.blit[1].width_px);
    TEST_ASSERT_EQUAL_UINT16(0, report.blit[1].src_x_px);
}
```

- [ ] Implement the test-only helper behind `#ifdef UI_OVERVIEW_WAVE_CACHE_TESTING` in the cache header:

```c
#ifdef UI_OVERVIEW_WAVE_CACHE_TESTING
void ui_overview_wave_cache_test_force_view_origin(ui_overview_wave_cache_t *cache, int origin_px);
#endif
```

- [ ] Add a runner guard in `tests\run_p4_host_tests.ps1`:

```powershell
Assert-FileDoesNotContain `
    -Path (Join-Path $Root "firmware/main-deck-p4/components/ui/ui_overview_wave_cache.c") `
    -Pattern "memmove\s*\(" `
    -Description "overview waveform cache must not use CPU memmove for steady scroll"
```

- [ ] Run the tests and confirm they fail for the right reason:

```powershell
.\tests\run_p4_host_tests.ps1
```

- [ ] Expected before implementation:

```text
ui_overview_wave_cache tests fail because bind_strip, OFFSET/EDGE reports, or segmented blits are not implemented yet.
```

- [ ] Commit:

```powershell
git add tests\ui_overview_wave_cache\test_ui_overview_wave_cache.c tests\run_p4_host_tests.ps1
git commit -m "test(ui): specify zero-copy overview waveform scrolling"
```

### 3. Add PPA Source-Region Blit API

- [ ] Extend `firmware\main-deck-p4\components\ui\include\ui_lvgl_backend.h`:

```c
esp_err_t ui_lvgl_backend_blit_rgb565_ppa270_region(const ui_overlay_rect_t *logical,
                                                    const uint16_t *src,
                                                    uint32_t src_w,
                                                    uint32_t src_h,
                                                    uint32_t src_x,
                                                    uint32_t src_y,
                                                    uint32_t block_w,
                                                    uint32_t block_h,
                                                    size_t src_bytes,
                                                    ui_lvgl_backend_blit_perf_t *perf);
```

- [ ] Implement argument validation in `ui_lvgl_backend.c` for both firmware and host builds:

```c
static bool rgb565_region_args_valid(const ui_overlay_rect_t *logical,
                                     const uint16_t *src,
                                     uint32_t src_w,
                                     uint32_t src_h,
                                     uint32_t src_x,
                                     uint32_t src_y,
                                     uint32_t block_w,
                                     uint32_t block_h,
                                     size_t src_bytes)
{
    if (!logical || !src || src_w == 0 || src_h == 0 || block_w == 0 || block_h == 0) {
        return false;
    }
    if (src_x >= src_w || src_y >= src_h) {
        return false;
    }
    if (block_w > src_w - src_x || block_h > src_h - src_y) {
        return false;
    }
    const size_t required = (size_t)src_w * (size_t)src_h * sizeof(uint16_t);
    return src_bytes >= required;
}
```

- [ ] Update the internal PPA helper to accept `src_x`, `src_y`, `block_w`, and `block_h`:

```c
in.pic_w = src_w;
in.pic_h = src_h;
in.block_w = block_w;
in.block_h = block_h;
in.block_offset_x = src_x;
in.block_offset_y = src_y;
```

- [ ] Keep the existing `ui_lvgl_backend_blit_rgb565_ppa270()` as a wrapper:

```c
return ui_lvgl_backend_blit_rgb565_ppa270_region(logical,
                                                 src,
                                                 src_w,
                                                 src_h,
                                                 0,
                                                 0,
                                                 src_w,
                                                 src_h,
                                                 src_bytes,
                                                 perf);
```

- [ ] For host builds, return `ESP_ERR_NOT_SUPPORTED` only after validating arguments, so invalid inputs still fail with `ESP_ERR_INVALID_ARG`.

- [ ] Build P4 to catch API and PPA struct mistakes:

```powershell
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1
cd D:\Documents\DDJ-FFL4\firmware\main-deck-p4
idf.py build
```

- [ ] Commit:

```powershell
cd D:\Documents\DDJ-FFL4
git add firmware\main-deck-p4\components\ui\include\ui_lvgl_backend.h firmware\main-deck-p4\components\ui\ui_lvgl_backend.c
git commit -m "feat(ui): add rgb565 source-region ppa blit"
```

### 4. Add Renderer Support for Decoupled Logical and Destination Columns

- [ ] Extend `ui_overview_renderer.h` with a column-span API that lets the cache render logical waveform columns into arbitrary physical strip positions:

```c
void ui_overview_renderer_draw_main_rgb565_column_span(uint16_t *pixels,
                                                       int stride_px,
                                                       int height_px,
                                                       int dest_x_px,
                                                       int logical_x_px,
                                                       int column_count,
                                                       int logical_width_px,
                                                       const ui_overview_wave_source_t *source,
                                                       uint32_t center_ms,
                                                       uint32_t window_ms,
                                                       const uint16_t *palette,
                                                       size_t palette_count);
```

- [ ] Implement it by reusing the existing per-column renderer logic. The sample-time calculation must use `logical_x_px + i` and `logical_width_px`; the write location must use `dest_x_px + i`.

```c
for (int i = 0; i < column_count; ++i) {
    const int logical_x = logical_x_px + i;
    const int dest_x = dest_x_px + i;
    draw_one_rgb565_column(pixels,
                           stride_px,
                           height_px,
                           dest_x,
                           logical_x,
                           logical_width_px,
                           source,
                           center_ms,
                           window_ms,
                           palette,
                           palette_count);
}
```

- [ ] Keep the existing column-range API as a wrapper where `dest_x_px == logical_x_px`.

- [ ] Add a host test in `tests\ui_overview_renderer\test_ui_overview_renderer.c`:

```c
static void test_column_span_can_render_logical_columns_to_different_destination(void)
{
    uint16_t pixels[64 * 16];
    memset(pixels, 0x00, sizeof(pixels));

    ui_overview_renderer_draw_main_rgb565_column_span(pixels,
                                                      64,
                                                      16,
                                                      40,
                                                      4,
                                                      8,
                                                      64,
                                                      &s_source,
                                                      10000,
                                                      8000,
                                                      s_palette,
                                                      ARRAY_SIZE(s_palette));

    TEST_ASSERT_EQUAL_HEX16(0x0000, pixels[8]);
    TEST_ASSERT_NOT_EQUAL_HEX16(0x0000, pixels[40]);
}
```

- [ ] Run host tests:

```powershell
cd D:\Documents\DDJ-FFL4
.\tests\run_p4_host_tests.ps1
```

- [ ] Commit:

```powershell
git add firmware\main-deck-p4\components\ui\include\ui_overview_renderer.h firmware\main-deck-p4\components\ui\ui_overview_renderer.c tests\ui_overview_renderer\test_ui_overview_renderer.c
git commit -m "feat(ui): render waveform columns into cache spans"
```

### 5. Refactor Waveform Cache into a Circular RGB565 Strip

- [ ] Update `ui_overview_wave_cache.h` constants and report structures:

```c
#define UI_OVERVIEW_WAVE_CACHE_MAX_BLITS 2
#define UI_OVERVIEW_WAVE_CACHE_MARGIN_PX 128
#define UI_OVERVIEW_WAVE_CACHE_EDGE_BATCH_PX 32

typedef enum {
    UI_OVERVIEW_WAVE_CACHE_NONE = 0,
    UI_OVERVIEW_WAVE_CACHE_FULL,
    UI_OVERVIEW_WAVE_CACHE_OFFSET,
    UI_OVERVIEW_WAVE_CACHE_EDGE,
} ui_overview_wave_cache_update_kind_t;

typedef struct {
    uint16_t src_x_px;
    uint16_t dst_x_px;
    uint16_t width_px;
} ui_overview_wave_cache_blit_t;

typedef struct {
    ui_overview_wave_cache_update_kind_t kind;
    bool blit_required;
    uint16_t columns_rendered;
    int scroll_dx_px;
    uint8_t blit_count;
    uint16_t blit_height_px;
    ui_overview_wave_cache_blit_t blit[UI_OVERVIEW_WAVE_CACHE_MAX_BLITS];
} ui_overview_wave_cache_report_t;
```

- [ ] Extend `ui_overview_wave_cache_t` with strip and ring metadata:

```c
struct ui_overview_wave_cache {
    uint16_t *pixels;
    int stride_px;
    int strip_width_px;
    int view_width_px;
    int height_px;
    int margin_px;
    int ring_head_px;
    int view_origin_px;
    int64_t strip_start_ms_q16;
    int64_t ms_per_px_q16;
    uint32_t last_center_ms;
    uint32_t last_window_ms;
    uint32_t source_generation;
    const uint16_t *palette;
    size_t palette_count;
    bool valid;
};
```

- [ ] Add `ui_overview_wave_cache_bind_strip()`:

```c
bool ui_overview_wave_cache_bind_strip(ui_overview_wave_cache_t *cache,
                                       uint16_t *pixels,
                                       int stride_px,
                                       int strip_width_px,
                                       int view_width_px,
                                       int height_px,
                                       int margin_px,
                                       const uint16_t *palette,
                                       size_t palette_count);
```

- [ ] Keep the existing bind function as a compatibility wrapper:

```c
bool ui_overview_wave_cache_bind(ui_overview_wave_cache_t *cache,
                                 uint16_t *pixels,
                                 int stride_px,
                                 int width_px,
                                 int height_px,
                                 const uint16_t *palette,
                                 size_t palette_count)
{
    return ui_overview_wave_cache_bind_strip(cache,
                                             pixels,
                                             stride_px,
                                             width_px,
                                             width_px,
                                             height_px,
                                             0,
                                             palette,
                                             palette_count);
}
```

- [ ] Remove the old physical scroll helper entirely. `ui_overview_wave_cache.c` must not contain `memmove(`.

- [ ] Implement full strip rebuild:

```c
static void rebuild_full_strip(ui_overview_wave_cache_t *cache,
                               const ui_overview_wave_source_t *source,
                               uint32_t center_ms,
                               uint32_t window_ms,
                               ui_overview_wave_cache_report_t *report)
{
    const uint32_t strip_window_ms = scale_window_ms(window_ms,
                                                    cache->strip_width_px,
                                                    cache->view_width_px);

    cache->ring_head_px = 0;
    cache->view_origin_px = (cache->strip_width_px - cache->view_width_px) / 2;
    cache->ms_per_px_q16 = ((int64_t)window_ms << 16) / cache->view_width_px;
    cache->strip_start_ms_q16 = ((int64_t)center_ms << 16) -
                                ((int64_t)cache->strip_width_px * cache->ms_per_px_q16) / 2;

    ui_overview_renderer_draw_main_rgb565_column_span(cache->pixels,
                                                      cache->stride_px,
                                                      cache->height_px,
                                                      0,
                                                      0,
                                                      cache->strip_width_px,
                                                      cache->strip_width_px,
                                                      source,
                                                      center_ms,
                                                      strip_window_ms,
                                                      cache->palette,
                                                      cache->palette_count);

    report->kind = UI_OVERVIEW_WAVE_CACHE_FULL;
    report->columns_rendered = (uint16_t)cache->strip_width_px;
    build_blit_segments(cache, report);
}
```

- [ ] Implement steady offset update:

```c
static bool update_view_origin_only(ui_overview_wave_cache_t *cache,
                                    uint32_t center_ms,
                                    ui_overview_wave_cache_report_t *report)
{
    const int64_t desired_left_q16 = ((int64_t)center_ms << 16) -
                                    ((int64_t)cache->view_width_px * cache->ms_per_px_q16) / 2;
    const int new_origin = (int)((desired_left_q16 - cache->strip_start_ms_q16 +
                                 (cache->ms_per_px_q16 / 2)) /
                                cache->ms_per_px_q16);

    if (new_origin < cache->margin_px / 2 ||
        new_origin + cache->view_width_px > cache->strip_width_px - cache->margin_px / 2) {
        return false;
    }

    cache->view_origin_px = new_origin;
    report->kind = UI_OVERVIEW_WAVE_CACHE_OFFSET;
    report->columns_rendered = 0;
    build_blit_segments(cache, report);
    return true;
}
```

- [ ] Implement edge advance in batches when the view approaches the safe margin:

```c
static void advance_right_edge(ui_overview_wave_cache_t *cache,
                               const ui_overview_wave_source_t *source,
                               uint32_t center_ms,
                               uint32_t window_ms,
                               ui_overview_wave_cache_report_t *report)
{
    const int batch = UI_OVERVIEW_WAVE_CACHE_EDGE_BATCH_PX;
    const int old_head = cache->ring_head_px;
    cache->ring_head_px = wrap_px(cache, cache->ring_head_px + batch);
    cache->strip_start_ms_q16 += (int64_t)batch * cache->ms_per_px_q16;
    cache->view_origin_px -= batch;

    const int logical_start = cache->strip_width_px - batch;
    const int dest_start = wrap_px(cache, old_head);
    render_wrapped_column_span(cache,
                               source,
                               dest_start,
                               logical_start,
                               batch,
                               center_ms,
                               window_ms);

    report->kind = UI_OVERVIEW_WAVE_CACHE_EDGE;
    report->columns_rendered = (uint16_t)batch;
    build_blit_segments(cache, report);
}
```

- [ ] Implement left-edge advance for reverse seek/jog movement using the same batch size and wrapped destination rendering.

- [ ] Implement `build_blit_segments()` so a visible window crossing the physical ring end becomes two blits:

```c
static void build_blit_segments(ui_overview_wave_cache_t *cache,
                                ui_overview_wave_cache_report_t *report)
{
    const int physical_start = wrap_px(cache, cache->ring_head_px + cache->view_origin_px);
    const int first = MIN(cache->view_width_px, cache->strip_width_px - physical_start);

    report->blit_required = true;
    report->blit_height_px = (uint16_t)cache->height_px;
    report->blit_count = 1;
    report->blit[0] = (ui_overview_wave_cache_blit_t) {
        .src_x_px = (uint16_t)physical_start,
        .dst_x_px = 0,
        .width_px = (uint16_t)first,
    };

    if (first < cache->view_width_px) {
        report->blit_count = 2;
        report->blit[1] = (ui_overview_wave_cache_blit_t) {
            .src_x_px = 0,
            .dst_x_px = (uint16_t)first,
            .width_px = (uint16_t)(cache->view_width_px - first),
        };
    }
}
```

- [ ] Run host tests:

```powershell
cd D:\Documents\DDJ-FFL4
.\tests\run_p4_host_tests.ps1
```

- [ ] Expected result:

```text
ui_overview_wave_cache PASS
ui_overview_renderer PASS
runner guard confirms no memmove in ui_overview_wave_cache.c
```

- [ ] Commit:

```powershell
git add firmware\main-deck-p4\components\ui\include\ui_overview_wave_cache.h firmware\main-deck-p4\components\ui\ui_overview_wave_cache.c tests\ui_overview_wave_cache\test_ui_overview_wave_cache.c tests\run_p4_host_tests.ps1
git commit -m "perf(ui): make overview waveform cache zero-copy"
```

### 6. Integrate Segmented Blits into Overview Screen

- [ ] Increase the per-deck waveform buffer allocation in `ui_overview.c` from visible width to strip width:

```c
#define OVERVIEW_WAVE_STRIP_MARGIN_PX UI_OVERVIEW_WAVE_CACHE_MARGIN_PX
#define OVERVIEW_WAVE_STRIP_W (OVERVIEW_CV_W + (OVERVIEW_WAVE_STRIP_MARGIN_PX * 2))
```

- [ ] Allocate `OVERVIEW_WAVE_STRIP_W * OVERVIEW_CV_H * sizeof(uint16_t)` per deck.

- [ ] Bind each cache with `ui_overview_wave_cache_bind_strip()`:

```c
ui_overview_wave_cache_bind_strip(&state->wave_cache,
                                  state->wave_overlay,
                                  OVERVIEW_WAVE_STRIP_W,
                                  OVERVIEW_WAVE_STRIP_W,
                                  OVERVIEW_CV_W,
                                  OVERVIEW_CV_H,
                                  OVERVIEW_WAVE_STRIP_MARGIN_PX,
                                  s_wave_rgb565_palette,
                                  ARRAY_SIZE(s_wave_rgb565_palette));
```

- [ ] Replace the single full-buffer blit with a segment loop:

```c
for (uint8_t i = 0; i < report.blit_count; ++i) {
    const ui_overview_wave_cache_blit_t *seg = &report.blit[i];
    ui_overlay_rect_t logical = ui_overview_wave_logical_rect(panel, deck);
    logical.x += seg->dst_x_px;
    logical.w = seg->width_px;

    esp_err_t err = ui_lvgl_backend_blit_rgb565_ppa270_region(&logical,
                                                              state->wave_overlay,
                                                              OVERVIEW_WAVE_STRIP_W,
                                                              OVERVIEW_CV_H,
                                                              seg->src_x_px,
                                                              0,
                                                              seg->width_px,
                                                              OVERVIEW_CV_H,
                                                              state->wave_overlay_bytes,
                                                              &perf);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "overview waveform segment blit failed deck=%d seg=%u err=%s",
                 deck,
                 i,
                 esp_err_to_name(err));
        break;
    }
}
```

- [ ] Update diagnostics to distinguish cache CPU work from PPA blit time:

```c
ESP_LOGI(TAG,
         "overview main cache deck=%d kind=%s columns=%u blits=%u cache_us=%" PRIu32 " ppa_us=%" PRIu32,
         deck + 1,
         ui_overview_wave_cache_kind_name(report.kind),
         report.columns_rendered,
         report.blit_count,
         cache_us,
         ppa_total_us);
```

- [ ] Add a guard to keep visible waveform dimensions unchanged:

```c
static_assert(OVERVIEW_WAVE_STRIP_W > OVERVIEW_CV_W, "wave strip must be wider than visible canvas");
```

- [ ] Build P4:

```powershell
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1
cd D:\Documents\DDJ-FFL4\firmware\main-deck-p4
idf.py build
```

- [ ] Commit:

```powershell
cd D:\Documents\DDJ-FFL4
git add firmware\main-deck-p4\components\ui\ui_overview.c
git commit -m "perf(ui): blit overview waveforms from circular strips"
```

### 7. Tune Strip Size, Edge Batch, and Frame Budget

- [ ] Start with:

```c
#define UI_OVERVIEW_WAVE_CACHE_MARGIN_PX 128
#define UI_OVERVIEW_WAVE_CACHE_EDGE_BATCH_PX 32
```

- [ ] If P4 memory pressure appears, reduce margin to 96 before reducing edge batch.

- [ ] If logs show too many `EDGE` updates, increase margin to 160 only if allocation stays stable.

- [ ] Add one log line on allocation:

```c
ESP_LOGI(TAG,
         "overview waveform strip deck=%d visible=%dx%d strip=%dx%d bytes=%u",
         deck + 1,
         OVERVIEW_CV_W,
         OVERVIEW_CV_H,
         OVERVIEW_WAVE_STRIP_W,
         OVERVIEW_CV_H,
         (unsigned)state->wave_overlay_bytes);
```

- [ ] Run P4 flash and capture serial logs:

```powershell
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1
cd D:\Documents\DDJ-FFL4\firmware\main-deck-p4
idf.py -p COM15 flash monitor
```

- [ ] Manual test:

1. Load a track on deck 1.
2. Start deck 1 playback.
3. Load a track on deck 2.
4. Start deck 2 playback.
5. Watch logs for at least 60 seconds.

- [ ] Expected steady logs:

```text
overview main cache deck=1 kind=OFFSET columns=0 ...
overview main cache deck=2 kind=OFFSET columns=0 ...
overview main cache deck=1 kind=EDGE columns=32 ... appears occasionally, not every frame
overview main cache deck=2 kind=EDGE columns=32 ... appears occasionally, not every frame
```

- [ ] Expected performance target:

```text
OFFSET cache_us under 500 us per deck
EDGE cache_us materially below old 5-7 ms steady path
ui_update interval normally under 24-28 ms with two playing decks
no watchdog, panic, or spontaneous reboot
```

- [ ] Commit tuning changes:

```powershell
cd D:\Documents\DDJ-FFL4
git add firmware\main-deck-p4\components\ui\include\ui_overview_wave_cache.h firmware\main-deck-p4\components\ui\ui_overview.c
git commit -m "perf(ui): tune overview waveform strip budget"
```

### 8. Document the New Rendering Model

- [ ] Update `docs\DEVELOPMENT_PLAN.md` with a short section under P4 UI performance:

```markdown
### Overview Waveform Rendering

The overview screen uses per-deck RGB565 circular waveform strips. Steady
playback scrolls by changing the PPA source offset instead of moving pixels on
the CPU. The cache renders only edge batches when the visible window approaches
the strip margin. Full strip rebuilds are reserved for load/source/window
changes and large seeks.
```

- [ ] Include measured before/after numbers from COM15 logs.

- [ ] Commit:

```powershell
git add docs\DEVELOPMENT_PLAN.md
git commit -m "docs: document overview waveform zero-copy scrolling"
```

### 9. Final Verification

- [ ] Run host tests:

```powershell
cd D:\Documents\DDJ-FFL4
.\tests\run_p4_host_tests.ps1
```

- [ ] Run P4 build:

```powershell
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1
cd D:\Documents\DDJ-FFL4\firmware\main-deck-p4
idf.py build
```

- [ ] Run final whitespace/status checks:

```powershell
cd D:\Documents\DDJ-FFL4
git diff --check
git status --short --untracked-files=all
```

- [ ] Confirm generated artifacts are not staged:

```powershell
git status --short --ignored | rg "build/|managed_components/|sdkconfig|dependencies.lock|\.exe"
```

- [ ] Expected final output:

```text
Host runner passes.
P4 build passes.
git diff --check has no output.
git status shows only intentional source/doc changes before final commit, then clean after commits.
No build artifacts staged.
```

## Acceptance Criteria

- `ui_overview_wave_cache.c` contains no `memmove(`.
- Steady playback returns `UI_OVERVIEW_WAVE_CACHE_OFFSET` with `columns_rendered == 0`.
- Edge updates render a bounded batch of columns, not a full visible waveform.
- Segment reports support one-blit and two-blit wrap cases.
- Overview screen uses `ui_lvgl_backend_blit_rgb565_ppa270_region()` for waveform strips.
- One active deck remains fluid.
- Two active decks no longer trigger visible periodic jank or reboot.
- COM15 logs show no panic, watchdog timeout, brownout, or unexpected reset during a 60-second dual-deck playback test.
- Host runner and P4 build pass.

## Rollback Plan

If the circular strip path causes PPA corruption or panel-coordinate regressions:

1. Keep the source-region backend API and renderer column-span API because they are independently useful.
2. Disable circular edge advance behind a compile-time flag:

```c
#define UI_OVERVIEW_WAVE_CACHE_USE_CIRCULAR_STRIP 0
```

3. Fall back to full strip rebuild plus source-offset blit while debugging wrap handling.
4. Do not reintroduce CPU `memmove()` into the steady playback path.

## Notes for Implementation

- The display is physically rotated; always use the existing `ui_overlay_map_ppa270()` path instead of inventing new coordinate transforms.
- PPA source-region blits must use source image dimensions for `pic_w` and `pic_h`, not visible segment dimensions.
- Segment logical destination width must match the segment width before mapping to physical coordinates.
- The cache must own waveform movement; `ui_overview.c` should only allocate buffers, call update, and execute the reported blits.
- Keep diagnostics until hardware validation proves the fix. Remove or lower log verbosity only after stable COM15 measurements.
