# UI Polish Overview Library Implementation Plan

> Superseded note, 2026-06-18: active DDJ-FFL4 firmware no longer exposes the
> Settings link-mode control or Library `JOINED` source selector. The remote
> library/cache portions of this historical plan are parked until a future
> remote-link re-enable pass.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Polish the CDJ deck `OVERVIEW` and `LIBRARY` screens so playback, waveform, beat, source, cache and load states are easier to read at 800x480 without changing media or audio behavior.

**Architecture:** Keep the work inside the existing LVGL UI component. Add a few shared theme tokens and small local style helpers, then apply them only to Overview and Library construction/update paths. Existing data flow through `deck_core`, `audio_engine`, `media_catalog`, `remote_cache` and `library` remains unchanged.

**Tech Stack:** ESP-IDF C firmware, LVGL, existing `firmware/main-deck-p4/components/ui` component, PowerShell, `idf.py build`.

---

## Files

- Modify: `firmware/main-deck-p4/components/ui/ui_theme.h`
  - Add shared colors for subdued panels, amber cue/status, red error/playhead and table row surfaces.
- Modify: `firmware/main-deck-p4/components/ui/ui.c`
  - Add local LVGL styles for panel frames, primary buttons, amber buttons and secondary action buttons.
  - Add stable object handles for the Library load button and load hint/status label.
  - Update `ui_update_library_source_label()` to use short source labels that fit the right rail.
  - Update `library_load_event_cb()` to visually lock and unlock the load button while caching/loading.
  - Polish `create_screen_overview()` layout, labels, waveform frames and beat pulse row.
  - Polish `create_screen_library()` table colors, right rail, source buttons, load button and sort controls.
- Reference only: `docs/superpowers/specs/2026-05-26-ui-polish-overview-library-design.md`
  - Confirms scope and acceptance criteria.

## Validation Commands

Run all commands from `D:\AI\CDJ-XXX\repo` unless a step says otherwise.

- Firmware build:

```powershell
cd D:\AI\CDJ-XXX\repo\firmware\main-deck-p4
idf.py build
```

Expected result: command exits with code `0`. Existing external `esp_codec_dev` Kconfig warning may remain. No new compiler warnings should mention `components/ui/ui.c`.

- Whitespace check:

```powershell
cd D:\AI\CDJ-XXX\repo
git diff --check
```

Expected result: no output and exit code `0`.

---

### Task 1: Add Theme Tokens And UI Object Handles

**Files:**
- Modify: `firmware/main-deck-p4/components/ui/ui_theme.h`
- Modify: `firmware/main-deck-p4/components/ui/ui.c`

- [ ] **Step 1: Add shared color tokens**

In `firmware/main-deck-p4/components/ui/ui_theme.h`, append these tokens after the current accent block:

```c
#define COL_AMBER      lv_color_hex(0xFFAB00)  // cue / paused / waiting state
#define COL_RED        lv_color_hex(0xFF1744)  // errors and playhead emphasis
#define COL_PANEL_DK   lv_color_hex(0x0F1114)  // dense work panel background
#define COL_TABLE_ROW  lv_color_hex(0x121417)  // library row surface
#define COL_TABLE_ALT  lv_color_hex(0x171B20)  // subtle selected/active surface
#define COL_DISABLED   lv_color_hex(0x2B3036)  // disabled action fill
```

This replaces repeated inline hexes already used in `ui.c`; it does not change behavior.

- [ ] **Step 2: Add local object handles**

In `firmware/main-deck-p4/components/ui/ui.c`, near the existing Library widget state:

```c
static lv_obj_t *s_label_link_mode = NULL;
static lv_obj_t *s_label_library_source = NULL;
```

replace it with:

```c
static lv_obj_t *s_label_link_mode = NULL;
static lv_obj_t *s_label_library_source = NULL;
static lv_obj_t *s_btn_library_load = NULL;
static lv_obj_t *s_label_library_hint = NULL;
```

These handles let the load path update the visible right rail without adding a new state machine.

- [ ] **Step 3: Build to catch declaration mistakes**

Run:

```powershell
cd D:\AI\CDJ-XXX\repo\firmware\main-deck-p4
idf.py build
```

Expected result: build exits with code `0`.

- [ ] **Step 4: Commit the theme/object-handle base**

Run:

```powershell
cd D:\AI\CDJ-XXX\repo
git add firmware/main-deck-p4/components/ui/ui_theme.h firmware/main-deck-p4/components/ui/ui.c
git commit -m "ui: add polish theme tokens"
```

Expected result: a commit is created with only the two UI files staged.

---

### Task 2: Add Small UI Style Helpers

**Files:**
- Modify: `firmware/main-deck-p4/components/ui/ui.c`

- [ ] **Step 1: Extend style declarations**

In the style declaration block in `ui.c`, replace:

```c
static lv_style_t s_style_screen_bg;
static lv_style_t s_style_btn_neon;
static lv_style_t s_style_pressed;   // color-agnostic touch feedback (dim on press)
```

with:

```c
static lv_style_t s_style_screen_bg;
static lv_style_t s_style_panel_frame;
static lv_style_t s_style_btn_primary;
static lv_style_t s_style_btn_amber;
static lv_style_t s_style_btn_secondary;
static lv_style_t s_style_btn_disabled;
static lv_style_t s_style_btn_neon;
static lv_style_t s_style_pressed;   // color-agnostic touch feedback (dim on press)
```

Keep `s_style_btn_neon` during this pass so existing screens outside Overview/Library remain stable.

- [ ] **Step 2: Initialize the new styles**

Inside the existing style initialization function, after `s_style_screen_bg` is initialized and before `s_style_btn_neon`, insert:

```c
    lv_style_init(&s_style_panel_frame);
    lv_style_set_bg_color(&s_style_panel_frame, COL_PANEL_DK);
    lv_style_set_bg_opa(&s_style_panel_frame, LV_OPA_COVER);
    lv_style_set_border_width(&s_style_panel_frame, 1);
    lv_style_set_border_color(&s_style_panel_frame, COL_BORDER_LT);
    lv_style_set_radius(&s_style_panel_frame, 4);
    lv_style_set_pad_all(&s_style_panel_frame, 0);

    lv_style_init(&s_style_btn_primary);
    lv_style_set_bg_color(&s_style_btn_primary, COL_GREEN);
    lv_style_set_bg_opa(&s_style_btn_primary, LV_OPA_COVER);
    lv_style_set_text_color(&s_style_btn_primary, COL_ON_ACCENT);
    lv_style_set_border_width(&s_style_btn_primary, 1);
    lv_style_set_border_color(&s_style_btn_primary, lv_color_hex(0x6DFFB1));
    lv_style_set_radius(&s_style_btn_primary, 6);

    lv_style_init(&s_style_btn_amber);
    lv_style_set_bg_color(&s_style_btn_amber, COL_AMBER);
    lv_style_set_bg_opa(&s_style_btn_amber, LV_OPA_COVER);
    lv_style_set_text_color(&s_style_btn_amber, COL_ON_ACCENT);
    lv_style_set_border_width(&s_style_btn_amber, 1);
    lv_style_set_border_color(&s_style_btn_amber, lv_color_hex(0xFFD166));
    lv_style_set_radius(&s_style_btn_amber, 6);

    lv_style_init(&s_style_btn_secondary);
    lv_style_set_bg_color(&s_style_btn_secondary, COL_SURFACE);
    lv_style_set_bg_opa(&s_style_btn_secondary, LV_OPA_COVER);
    lv_style_set_text_color(&s_style_btn_secondary, COL_TEXT_MUTED);
    lv_style_set_border_width(&s_style_btn_secondary, 1);
    lv_style_set_border_color(&s_style_btn_secondary, COL_BORDER_LT);
    lv_style_set_radius(&s_style_btn_secondary, 5);

    lv_style_init(&s_style_btn_disabled);
    lv_style_set_bg_color(&s_style_btn_disabled, COL_DISABLED);
    lv_style_set_bg_opa(&s_style_btn_disabled, LV_OPA_COVER);
    lv_style_set_text_color(&s_style_btn_disabled, COL_TEXT_DIM);
    lv_style_set_border_width(&s_style_btn_disabled, 1);
    lv_style_set_border_color(&s_style_btn_disabled, COL_BORDER);
    lv_style_set_radius(&s_style_btn_disabled, 6);
```

The styles use fixed radii no larger than 6 px, matching the existing utilitarian UI.

- [ ] **Step 3: Add helper functions**

After `ui_label_set_f2()` in `ui.c`, insert:

```c
static void ui_label_set_small_caps(lv_obj_t *label, const char *text, lv_color_t color)
{
    if (!label) {
        return;
    }
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
}

static void ui_library_set_load_busy(bool busy, const char *hint)
{
    if (s_btn_library_load) {
        if (busy) {
            lv_obj_add_state(s_btn_library_load, LV_STATE_DISABLED);
            lv_obj_add_style(s_btn_library_load, &s_style_btn_disabled, LV_PART_MAIN);
        } else {
            lv_obj_clear_state(s_btn_library_load, LV_STATE_DISABLED);
            lv_obj_remove_style(s_btn_library_load, &s_style_btn_disabled, LV_PART_MAIN);
        }
    }

    if (s_label_library_hint) {
        lv_label_set_text(s_label_library_hint, hint ? hint : "Select row\nthen LOAD");
    }
}
```

This helper is presentation-only. The existing `s_track_load_busy` boolean remains the source of truth for duplicate tap prevention.

- [ ] **Step 4: Build after helper insertion**

Run:

```powershell
cd D:\AI\CDJ-XXX\repo\firmware\main-deck-p4
idf.py build
```

Expected result: build exits with code `0` and no `ui.c` warnings.

- [ ] **Step 5: Commit style helpers**

Run:

```powershell
cd D:\AI\CDJ-XXX\repo
git add firmware/main-deck-p4/components/ui/ui.c
git commit -m "ui: add reusable polish styles"
```

Expected result: a commit is created with `ui.c` changes from this task.

---

### Task 3: Polish Overview Screen

**Files:**
- Modify: `firmware/main-deck-p4/components/ui/ui.c`

- [ ] **Step 1: Update PLAY button styling**

In `create_screen_overview()`, change the PLAY button style from `s_style_btn_neon` to `s_style_btn_primary`:

```c
    lv_obj_add_style(btn_play, &s_style_btn_primary, LV_PART_MAIN);
```

Keep size `140x50`, position `(20,20)` and the existing event callback.

- [ ] **Step 2: Update CUE button styling**

In `create_screen_overview()`, replace the CUE button style lines:

```c
    lv_obj_add_style(btn_cue, &s_style_btn_neon, LV_PART_MAIN);
    lv_obj_add_style(btn_cue, &s_style_pressed, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(btn_cue, lv_color_hex(0xFFAB00), LV_PART_MAIN); // amber — CUE
```

with:

```c
    lv_obj_add_style(btn_cue, &s_style_btn_amber, LV_PART_MAIN);
    lv_obj_add_style(btn_cue, &s_style_pressed, LV_STATE_PRESSED);
```

Keep size `140x50`, position `(640,20)` and the existing event callback.

- [ ] **Step 3: Apply panel frame style to overview waveform**

In `create_screen_overview()`, replace the overview waveform frame styling:

```c
    lv_obj_set_style_bg_color(wv_border, COL_SURFACE, LV_PART_MAIN);
    lv_obj_set_style_border_color(wv_border, COL_BORDER_LT, LV_PART_MAIN);
    lv_obj_set_style_border_width(wv_border, 1, LV_PART_MAIN);
```

with:

```c
    lv_obj_remove_style_all(wv_border);
    lv_obj_add_style(wv_border, &s_style_panel_frame, LV_PART_MAIN);
```

Keep size `420x80`, position `(180,10)` and padding `0`.

- [ ] **Step 4: Replace inline red playhead token**

In `create_screen_overview()`, replace:

```c
    lv_obj_set_style_bg_color(s_overview_playhead, lv_color_hex(0xFF1744), LV_PART_MAIN);
```

with:

```c
    lv_obj_set_style_bg_color(s_overview_playhead, COL_RED, LV_PART_MAIN);
```

This preserves the current 3 px playhead while moving the color into the shared palette.

- [ ] **Step 5: Make beat pulse row read as timing**

In the beat pulse creation loop, replace the current color block:

```c
        lv_obj_set_style_bg_color(s_beat_pulses[i], lv_color_hex(0x30343B), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_beat_pulses[i], LV_OPA_40, LV_PART_MAIN);
        lv_obj_set_style_border_color(s_beat_pulses[i], lv_color_hex(0x4A515C), LV_PART_MAIN);
        lv_obj_set_style_border_width(s_beat_pulses[i], 1, LV_PART_MAIN);
```

with:

```c
        lv_obj_set_style_bg_color(s_beat_pulses[i], COL_PANEL_DK, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_beat_pulses[i], LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(s_beat_pulses[i], COL_BORDER_LT, LV_PART_MAIN);
        lv_obj_set_style_border_width(s_beat_pulses[i], 1, LV_PART_MAIN);
```

Do not change the existing beat update logic in `ui_timer_cb()`.

- [ ] **Step 6: Shorten Overview labels**

In `create_screen_overview()`, replace:

```c
    lv_label_set_text(lbl_wv_info, "Low-Res Track Structure (PWAV)");
```

with:

```c
    lv_label_set_text(lbl_wv_info, "TRACK OVERVIEW");
```

and replace:

```c
    lv_label_set_text(lbl_zoom_title, "High-Res Scrolling Waveform Zoom (PWV3)");
```

with:

```c
    lv_label_set_text(lbl_zoom_title, "WAVEFORM ZOOM");
```

Keep both labels on Montserrat 12 and muted colors to avoid competing with the header metadata.

- [ ] **Step 7: Apply panel frame style to zoom container**

In `create_screen_overview()`, replace:

```c
    lv_obj_set_style_bg_color(zoom_container, lv_color_hex(0x050505), LV_PART_MAIN);
    lv_obj_set_style_border_color(zoom_container, COL_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_width(zoom_container, 1, LV_PART_MAIN);
```

with:

```c
    lv_obj_remove_style_all(zoom_container);
    lv_obj_add_style(zoom_container, &s_style_panel_frame, LV_PART_MAIN);
    lv_obj_set_style_bg_color(zoom_container, lv_color_hex(0x050505), LV_PART_MAIN);
```

Keep size `760x160`, position `(20,165)` and padding `0`.

- [ ] **Step 8: Build and inspect Overview changes**

Run:

```powershell
cd D:\AI\CDJ-XXX\repo\firmware\main-deck-p4
idf.py build
```

Expected result: build exits with code `0`; Overview event callbacks and waveform canvas allocation still compile for firmware and `WIN32` branches.

- [ ] **Step 9: Commit Overview polish**

Run:

```powershell
cd D:\AI\CDJ-XXX\repo
git add firmware/main-deck-p4/components/ui/ui.c
git commit -m "ui: polish overview deck surface"
```

Expected result: a commit is created with only Overview-related `ui.c` edits from this task.

---

### Task 4: Polish Library Screen And Load States

**Files:**
- Modify: `firmware/main-deck-p4/components/ui/ui.c`

- [ ] **Step 1: Shorten Library source labels**

Replace the body of `ui_update_library_source_label()` with:

```c
static void ui_update_library_source_label(void)
{
    if (!s_label_library_source) {
        return;
    }
#ifndef WIN32
    if (media_catalog_get_source() == MEDIA_SOURCE_REMOTE_LINK) {
        lv_label_set_text_fmt(s_label_library_source, "JOINED  %d TRACKS", media_catalog_count());
    } else {
        lv_label_set_text_fmt(s_label_library_source, "LOCAL USB  %d TRACKS", media_catalog_count());
    }
#else
    lv_label_set_text_fmt(s_label_library_source, "LOCAL USB  %d TRACKS", library_count());
#endif
}
```

The strings are stable, short and fit the right rail at x `630`.

- [ ] **Step 2: Lock load button during cache/load**

In `library_load_event_cb()`, after:

```c
    s_track_load_busy = true;
```

insert:

```c
    ui_library_set_load_busy(true, "LOAD IN\nPROGRESS");
```

In every return path after this point, call `ui_library_set_load_busy(false, NULL);` immediately before setting `s_track_load_busy = false;` or immediately before returning after a successful load.

For the firmware no-row error branch, replace:

```c
        s_track_load_busy = false;
        return;
```

with:

```c
        ui_library_set_load_busy(false, NULL);
        s_track_load_busy = false;
        return;
```

For the firmware load error branch, replace:

```c
        s_track_load_busy = false;
        return;
```

with:

```c
        ui_library_set_load_busy(false, status ? status : "LOAD ERR");
        s_track_load_busy = false;
        return;
```

At the successful end of the function, keep existing metadata updates and add:

```c
    ui_library_set_load_busy(false, "TRACK\nLOADED");
    s_track_load_busy = false;
```

If the `WIN32` branch already reaches a shared successful tail, use the shared tail; otherwise add the same two lines before leaving the `WIN32` block.

- [ ] **Step 3: Polish Library table surface**

In `create_screen_library()`, replace:

```c
    lv_obj_set_style_bg_color(s_library_table, lv_color_hex(0x121417), LV_PART_ITEMS);
    lv_obj_set_style_text_color(s_library_table, lv_color_hex(0xCCCCCC), LV_PART_ITEMS);
```

with:

```c
    lv_obj_set_style_bg_color(s_library_table, COL_TABLE_ROW, LV_PART_ITEMS);
    lv_obj_set_style_text_color(s_library_table, COL_TEXT_MUTED, LV_PART_ITEMS);
```

Replace the focused selected row fill:

```c
    lv_obj_set_style_bg_color(s_library_table, COL_ACCENT_DK, LV_PART_ITEMS | LV_STATE_FOCUSED);
```

with:

```c
    lv_obj_set_style_bg_color(s_library_table, COL_TABLE_ALT, LV_PART_ITEMS | LV_STATE_FOCUSED);
```

Then add:

```c
    lv_obj_set_style_border_color(s_library_table, COL_ACCENT, LV_PART_ITEMS | LV_STATE_FOCUSED);
```

This keeps the selected row visible without filling it with a dominant blue block.

- [ ] **Step 4: Style source buttons as selector controls**

In `create_screen_library()`, change the source button style from `s_style_btn_neon` to `s_style_btn_secondary` for both `btn_src_local` and `btn_src_join`:

```c
    lv_obj_add_style(btn_src_local, &s_style_btn_secondary, LV_PART_MAIN);
```

and:

```c
    lv_obj_add_style(btn_src_join, &s_style_btn_secondary, LV_PART_MAIN);
```

Change both source labels from `COL_ON_ACCENT` to `COL_TEXT`:

```c
    lv_obj_set_style_text_color(lbl_src_local, COL_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_src_join, COL_TEXT, LV_PART_MAIN);
```

The source selector no longer competes visually with the Load button.

- [ ] **Step 5: Capture and style the Load button handle**

In `create_screen_library()`, replace:

```c
    lv_obj_t *btn_load = lv_button_create(s_screens[1]);
    lv_obj_remove_style_all(btn_load);
    lv_obj_add_style(btn_load, &s_style_btn_neon, LV_PART_MAIN);
    lv_obj_add_style(btn_load, &s_style_pressed, LV_STATE_PRESSED);
    lv_obj_set_size(btn_load, 150, 50);
    lv_obj_set_pos(btn_load, 630, 72);
    lv_obj_add_event_cb(btn_load, library_load_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_remove_flag(btn_load, LV_OBJ_FLAG_CLICK_FOCUSABLE);

    lv_obj_t *lbl_load = lv_label_create(btn_load);
```

with:

```c
    s_btn_library_load = lv_button_create(s_screens[1]);
    lv_obj_remove_style_all(s_btn_library_load);
    lv_obj_add_style(s_btn_library_load, &s_style_btn_primary, LV_PART_MAIN);
    lv_obj_add_style(s_btn_library_load, &s_style_pressed, LV_STATE_PRESSED);
    lv_obj_set_size(s_btn_library_load, 150, 50);
    lv_obj_set_pos(s_btn_library_load, 630, 72);
    lv_obj_add_event_cb(s_btn_library_load, library_load_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_remove_flag(s_btn_library_load, LV_OBJ_FLAG_CLICK_FOCUSABLE);

    lv_obj_t *lbl_load = lv_label_create(s_btn_library_load);
```

Leave the label text as `LOAD TRACK`.

- [ ] **Step 6: Demote sort controls**

For `btn_sort_artist`, `btn_sort_name` and `btn_sort_bpm`, change each main style from `s_style_btn_neon` to `s_style_btn_secondary`.

For `lbl_sort_artist`, `lbl_sort_name` and `lbl_sort_bpm`, change text color from `COL_ON_ACCENT` to `COL_TEXT_MUTED`.

Keep button sizes and positions unchanged.

- [ ] **Step 7: Store the hint label handle**

In `create_screen_library()`, replace:

```c
    lv_obj_t *lbl_hint = lv_label_create(s_screens[1]);
    lv_label_set_text(lbl_hint, "Select row\nthen LOAD");
    lv_obj_set_style_text_font(lbl_hint, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_hint, COL_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_pos(lbl_hint, 630, 300);
```

with:

```c
    s_label_library_hint = lv_label_create(s_screens[1]);
    lv_label_set_text(s_label_library_hint, "Select row\nthen LOAD");
    lv_obj_set_style_text_font(s_label_library_hint, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_label_library_hint, COL_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_pos(s_label_library_hint, 630, 300);
```

This gives cache/load status a stable location.

- [ ] **Step 8: Build Library polish**

Run:

```powershell
cd D:\AI\CDJ-XXX\repo\firmware\main-deck-p4
idf.py build
```

Expected result: build exits with code `0`; no warnings mention `ui.c`; `library_load_event_cb()` has no path that leaves `s_track_load_busy` stuck after a completed or failed load.

- [ ] **Step 9: Commit Library polish**

Run:

```powershell
cd D:\AI\CDJ-XXX\repo
git add firmware/main-deck-p4/components/ui/ui.c
git commit -m "ui: polish library source and load states"
```

Expected result: a commit is created with Library-related UI edits.

---

### Task 5: Final Verification And Push Preparation

**Files:**
- Modify only if needed after verification: `firmware/main-deck-p4/components/ui/ui.c`
- Modify only if needed after verification: `firmware/main-deck-p4/components/ui/ui_theme.h`

- [ ] **Step 1: Confirm no stray whitespace errors**

Run:

```powershell
cd D:\AI\CDJ-XXX\repo
git diff --check
```

Expected result: no output and exit code `0`.

- [ ] **Step 2: Run final firmware build**

Run:

```powershell
cd D:\AI\CDJ-XXX\repo\firmware\main-deck-p4
idf.py build
```

Expected result: build exits with code `0`. If the known external `esp_codec_dev` Kconfig warning appears, keep it unchanged. No new `ui.c` warnings are acceptable.

- [ ] **Step 3: Inspect changed files**

Run:

```powershell
cd D:\AI\CDJ-XXX\repo
git diff --stat HEAD~3..HEAD
git status --short --branch
```

Expected result: only UI polish commits are ahead of `origin/main`, and the working tree is clean.

- [ ] **Step 4: Push after user approval**

Run this only after the user approves pushing the completed polish work:

```powershell
cd D:\AI\CDJ-XXX\repo
git push origin main
```

Expected result: `main` is pushed to `origin/main`.

---

## Self-Review

- Spec coverage: Overview hierarchy is covered by Task 3. Library scan/source/cache/load clarity is covered by Task 4. Shared style boundaries are covered by Tasks 1 and 2. Build and no-new-warning acceptance is covered by Task 5.
- Behavioral boundary: No task changes `deck_core`, `audio_engine`, `media_catalog`, `remote_cache`, C6 Wi-Fi, NVS, USB, SD or ANLZ parsing logic.
- Layout boundary: All positions and main sizes remain at the existing 800x480 layout values, reducing overlap risk while improving styling and text length.
