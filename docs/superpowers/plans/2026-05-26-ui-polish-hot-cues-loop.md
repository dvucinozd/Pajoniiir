# UI Polish Hot Cues Loop Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Polish the `HOT CUES` and `LOOP` screens so cue, hot-loop, empty-pad and loop-active states are readable at a glance without changing playback behavior.

**Architecture:** Keep the implementation inside the existing LVGL `ui.c` surface. Add small local presentation helpers and object handles, then reuse the shared styles introduced by the Overview/Library polish. Existing callbacks remain the behavioral boundary; the new code only updates colors, labels and active-state presentation.

**Tech Stack:** ESP-IDF C firmware, LVGL, existing `firmware/main-deck-p4/components/ui` component, PowerShell, `idf.py build`.

---

## Files

- Modify: `firmware/main-deck-p4/components/ui/ui.c`
  - Add loop-screen object handles and a small presentation-only active-loop mirror.
  - Add local helpers for hot-cue pad styling and loop button active/inactive styling.
  - Update `create_screen_hot_cues()` to use semantic cue/loop/empty styling instead of the rainbow palette.
  - Update `ui_update_hot_cues()` to apply cue, hot-loop and empty pad states consistently.
  - Update `create_screen_beat_loop()` to use secondary buttons by default, a red exit action, and a status label.
  - Update loop-related callbacks to refresh the loop status label and active button styling.
- Modify: `docs/development-plan.md`
  - Mark `HOT CUES` + `LOOP` polish as completed under the UI polish checklist after implementation and verification.

## Validation Commands

Run from `D:\AI\CDJ-XXX\repo` unless specified otherwise.

Firmware build:

```powershell
cd D:\AI\CDJ-XXX\repo\firmware\main-deck-p4
$activate = & "C:\Espressif\python_env\idf5.5_py3.13_env\Scripts\python.exe" "C:\Espressif\.espressif\v5.5.4\esp-idf\tools\activate.py" --export | Select-Object -First 1; . $activate; idf.py build
```

Expected result: exit code `0`, output includes `Project build complete`, and no warnings mention `components/ui/ui.c`.

Patch hygiene:

```powershell
cd D:\AI\CDJ-XXX\repo
git diff --check
```

Expected result: no output and exit code `0`.

---

### Task 1: Add Presentation Handles And Helpers

**Files:**
- Modify: `firmware/main-deck-p4/components/ui/ui.c`

- [ ] **Step 1: Add loop screen handles and active-beat mirror**

Near the existing hot-cue and loop state declarations, replace:

```c
static lv_obj_t *s_hot_cue_buttons[8];
static lv_obj_t *s_overview_cue_markers[8];
```

with:

```c
static lv_obj_t *s_hot_cue_buttons[8];
static lv_obj_t *s_overview_cue_markers[8];
static lv_obj_t *s_loop_buttons[6];
static lv_obj_t *s_label_loop_status = NULL;
static int       s_loop_active_beats = 0;
```

`s_loop_active_beats` is presentation-only. It mirrors the loop button that was tapped so the screen can highlight it; it does not replace `s_loop_active`, `s_loop_start_ms`, or `s_loop_end_ms`.

- [ ] **Step 2: Add a time-format helper**

After `ui_label_set_f2()` and before `ui_label_set_small_caps()`, insert:

```c
static void ui_format_time_cc(char *out, size_t out_sz, uint32_t ms)
{
    uint32_t secs = ms / 1000;
    uint32_t centis = (ms % 1000) / 10;
    snprintf(out, out_sz, "%02u:%02u.%02u",
             (unsigned)(secs / 60),
             (unsigned)(secs % 60),
             (unsigned)centis);
}
```

This keeps hot-cue time labels consistent across initial creation and metadata refresh.

- [ ] **Step 3: Add hot-cue pad styling helper**

After `ui_library_set_load_busy()`, insert:

```c
static void ui_style_hot_cue_pad(int index, bool is_loop, bool is_empty)
{
    if (index < 0 || index >= 8 || !s_hot_cue_buttons[index]) {
        return;
    }

    lv_obj_t *pad = s_hot_cue_buttons[index];
    lv_color_t accent = is_empty ? COL_BORDER_LT : (is_loop ? COL_AMBER : COL_GREEN);
    lv_color_t bg = is_empty ? COL_PANEL_DK : accent;
    lv_opa_t bg_opa = is_empty ? LV_OPA_COVER : LV_OPA_30;
    lv_color_t text = is_empty ? COL_TEXT_DIM : accent;
    lv_color_t time_text = is_empty ? COL_TEXT_DIM : COL_TEXT;

    lv_obj_set_style_bg_color(pad, bg, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(pad, bg_opa, LV_PART_MAIN);
    lv_obj_set_style_border_color(pad, accent, LV_PART_MAIN);
    lv_obj_set_style_border_width(pad, is_empty ? 1 : 2, LV_PART_MAIN);
    lv_obj_set_style_radius(pad, 6, LV_PART_MAIN);

    lv_obj_t *lbl_pad = lv_obj_get_child(pad, 0);
    if (lbl_pad) {
        lv_obj_set_style_text_color(lbl_pad, text, LV_PART_MAIN);
    }

    lv_obj_t *lbl_time = lv_obj_get_child(pad, 1);
    if (lbl_time) {
        lv_obj_set_style_text_color(lbl_time, time_text, LV_PART_MAIN);
    }
}
```

This helper only changes LVGL presentation for the existing button and label children.

- [ ] **Step 4: Add loop screen styling helper**

After `ui_style_hot_cue_pad()`, insert:

```c
static void ui_update_loop_screen_state(void)
{
    if (s_label_loop_status) {
        if (s_loop_active) {
            if (s_loop_active_beats > 0) {
                lv_label_set_text_fmt(s_label_loop_status, "ACTIVE: %d BEATS", s_loop_active_beats);
            } else {
                lv_label_set_text(s_label_loop_status, "ACTIVE LOOP");
            }
            lv_obj_set_style_text_color(s_label_loop_status, COL_ACCENT, LV_PART_MAIN);
        } else {
            lv_label_set_text(s_label_loop_status, "NO ACTIVE LOOP");
            lv_obj_set_style_text_color(s_label_loop_status, COL_TEXT_DIM, LV_PART_MAIN);
        }
    }

    static const int loop_beats[6] = {1, 2, 4, 8, 16, 32};
    for (int i = 0; i < 6; i++) {
        if (!s_loop_buttons[i]) {
            continue;
        }
        bool active = s_loop_active && s_loop_active_beats == loop_beats[i];
        lv_obj_set_style_bg_color(s_loop_buttons[i], active ? COL_ACCENT_DK : COL_SURFACE, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_loop_buttons[i], LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(s_loop_buttons[i], active ? COL_ACCENT : COL_BORDER_LT, LV_PART_MAIN);
        lv_obj_set_style_border_width(s_loop_buttons[i], active ? 2 : 1, LV_PART_MAIN);
        lv_obj_set_style_radius(s_loop_buttons[i], 6, LV_PART_MAIN);

        lv_obj_t *label = lv_obj_get_child(s_loop_buttons[i], 0);
        if (label) {
            lv_obj_set_style_text_color(label, active ? COL_TEXT : COL_TEXT_MUTED, LV_PART_MAIN);
        }
    }
}
```

This helper intentionally shows `ACTIVE LOOP` for hot loops where the active length may not match one of the six beat-loop buttons.

- [ ] **Step 5: Build after helper insertion**

Run:

```powershell
cd D:\AI\CDJ-XXX\repo\firmware\main-deck-p4
$activate = & "C:\Espressif\python_env\idf5.5_py3.13_env\Scripts\python.exe" "C:\Espressif\.espressif\v5.5.4\esp-idf\tools\activate.py" --export | Select-Object -First 1; . $activate; idf.py build
```

Expected result: build exits `0`, no `ui.c` unused-function or unused-variable warnings.

- [ ] **Step 6: Commit helper base**

Run:

```powershell
cd D:\AI\CDJ-XXX\repo
git add firmware/main-deck-p4/components/ui/ui.c
git commit -m "ui: add performance screen polish helpers"
```

Expected result: one commit containing only helper/handle scaffolding.

---

### Task 2: Polish Hot Cues Screen

**Files:**
- Modify: `firmware/main-deck-p4/components/ui/ui.c`

- [ ] **Step 1: Remove the rainbow palette from screen creation**

In `create_screen_hot_cues()`, delete the local `cue_colors[8]` array and the associated comment:

```c
    // Colors for the 8 Hot Cue pads (CDJ styles)
    static const uint32_t cue_colors[8] = {
        0xFF1744, // A: Red
        0xFF9100, // B: Orange
        0xFFEA00, // C: Yellow
        0x00E676, // D: Green
        0x00E5FF, // E: Cyan
        0x2979FF, // F: Blue
        0xD500F9, // G: Purple
        0xF50057  // H: Pink
    };
```

The pad language for this pass is semantic: green cue, amber loop, dim empty.

- [ ] **Step 2: Apply semantic default pad styling**

In the pad creation loop, replace:

```c
        // Dynamic colors using border/bg
        lv_obj_set_style_bg_color(s_hot_cue_buttons[i], lv_color_hex(cue_colors[i]), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_hot_cue_buttons[i], LV_OPA_30, LV_PART_MAIN);
        lv_obj_set_style_border_color(s_hot_cue_buttons[i], lv_color_hex(cue_colors[i]), LV_PART_MAIN);
        lv_obj_set_style_border_width(s_hot_cue_buttons[i], 2, LV_PART_MAIN);
        lv_obj_set_style_radius(s_hot_cue_buttons[i], 8, LV_PART_MAIN);
```

with:

```c
        lv_obj_remove_style_all(s_hot_cue_buttons[i]);
        lv_obj_add_style(s_hot_cue_buttons[i], &s_style_pressed, LV_STATE_PRESSED);
        ui_style_hot_cue_pad(i, false, false);
```

Keep `lv_obj_set_size`, `lv_obj_set_pos`, `lv_obj_set_user_data`, and `lv_obj_add_event_cb` unchanged.

- [ ] **Step 3: Update initial pad label styling**

In `create_screen_hot_cues()`, replace the initial pad-label color:

```c
        lv_obj_set_style_text_color(lbl_pad, lv_color_hex(cue_colors[i]), LV_PART_MAIN);
```

with:

```c
        lv_obj_set_style_text_color(lbl_pad, COL_GREEN, LV_PART_MAIN);
```

Keep label text `CUE <letter>`, Montserrat 16, and top-left alignment.

- [ ] **Step 4: Use the time formatter for initial labels**

Replace:

```c
        uint32_t secs = s_hot_cue_positions[i] / 1000;
        lv_label_set_text_fmt(lbl_time, "%02u:%02u.00", (unsigned)(secs / 60), (unsigned)(secs % 60));
```

with:

```c
        char time_buf[16];
        ui_format_time_cc(time_buf, sizeof(time_buf), s_hot_cue_positions[i]);
        lv_label_set_text(lbl_time, time_buf);
```

Keep the time label font, color and bottom-right alignment.

- [ ] **Step 5: Apply semantic states in `ui_update_hot_cues()` for found cues**

In the `if (found)` branch, replace the inline label/color state block:

```c
            if (type == 2) {
                // Hot Loop (Neon Orange)
                if (lbl_pad) {
                    lv_label_set_text_fmt(lbl_pad, "LOOP %c", 'A' + i);
                }
                lv_obj_set_style_bg_color(s_hot_cue_buttons[i], lv_color_hex(0xFF9100), LV_PART_MAIN);
                lv_obj_set_style_border_color(s_hot_cue_buttons[i], lv_color_hex(0xFF9100), LV_PART_MAIN);
            } else {
                // Hot Cue (Neon Green)
                if (lbl_pad) {
                    lv_label_set_text_fmt(lbl_pad, "CUE %c", 'A' + i);
                }
                lv_obj_set_style_bg_color(s_hot_cue_buttons[i], COL_GREEN, LV_PART_MAIN);
                lv_obj_set_style_border_color(s_hot_cue_buttons[i], COL_GREEN, LV_PART_MAIN);
            }
            lv_obj_set_style_bg_opa(s_hot_cue_buttons[i], LV_OPA_60, LV_PART_MAIN);
```

with:

```c
            bool is_loop = (type == 2);
            if (lbl_pad) {
                lv_label_set_text_fmt(lbl_pad, is_loop ? "LOOP %c" : "CUE %c", 'A' + i);
            }
            ui_style_hot_cue_pad(i, is_loop, false);
```

Keep the existing cue marker update block below it. Marker colors should continue using amber for loop markers and green for cue markers.

- [ ] **Step 6: Format found cue times through the helper**

In the `if (found)` branch, replace:

```c
            uint32_t secs = pos / 1000;
            uint32_t ms = (pos % 1000) / 10;
            
            lv_obj_t *lbl_time = lv_obj_get_child(s_hot_cue_buttons[i], 1);
            if (lbl_time) {
                lv_label_set_text_fmt(lbl_time, "%02u:%02u.%02u", (unsigned)(secs / 60), (unsigned)(secs % 60), (unsigned)ms);
            }
```

with:

```c
            char time_buf[16];
            ui_format_time_cc(time_buf, sizeof(time_buf), pos);

            lv_obj_t *lbl_time = lv_obj_get_child(s_hot_cue_buttons[i], 1);
            if (lbl_time) {
                lv_label_set_text(lbl_time, time_buf);
            }
```

- [ ] **Step 7: Apply empty state text**

In the `else if (has_real_cues)` branch, replace:

```c
            if (lbl_time) {
                lv_label_set_text(lbl_time, "--:--.--");
            }
```

with:

```c
            if (lbl_time) {
                lv_label_set_text(lbl_time, "EMPTY");
            }
```

Replace:

```c
            if (lbl_pad) {
                lv_label_set_text_fmt(lbl_pad, "CUE %c (EMPTY)", 'A' + i);
            }
            lv_obj_set_style_bg_color(s_hot_cue_buttons[i], lv_color_hex(0x0078FF), LV_PART_MAIN); // fallback color
            lv_obj_set_style_bg_opa(s_hot_cue_buttons[i], LV_OPA_10, LV_PART_MAIN);
```

with:

```c
            if (lbl_pad) {
                lv_label_set_text_fmt(lbl_pad, "CUE %c", 'A' + i);
            }
            ui_style_hot_cue_pad(i, false, true);
```

This removes the long `CUE A (EMPTY)` text that risks fitting poorly on the pad.

- [ ] **Step 8: Apply fallback cue state consistently**

In the fallback `else` branch, after setting label text, replace:

```c
            lv_obj_set_style_bg_color(s_hot_cue_buttons[i], COL_GREEN, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(s_hot_cue_buttons[i], LV_OPA_30, LV_PART_MAIN);
```

with:

```c
            ui_style_hot_cue_pad(i, false, false);
```

Keep fallback cue positions and overview marker hiding unchanged.

- [ ] **Step 9: Build Hot Cues polish**

Run:

```powershell
cd D:\AI\CDJ-XXX\repo\firmware\main-deck-p4
$activate = & "C:\Espressif\python_env\idf5.5_py3.13_env\Scripts\python.exe" "C:\Espressif\.espressif\v5.5.4\esp-idf\tools\activate.py" --export | Select-Object -First 1; . $activate; idf.py build
```

Expected result: build exits `0`, no `ui.c` warnings.

- [ ] **Step 10: Commit Hot Cues polish**

Run:

```powershell
cd D:\AI\CDJ-XXX\repo
git add firmware/main-deck-p4/components/ui/ui.c
git commit -m "ui: polish hot cue pad states"
```

Expected result: one commit containing only Hot Cues presentation changes.

---

### Task 3: Polish Loop Screen And Active State

**Files:**
- Modify: `firmware/main-deck-p4/components/ui/ui.c`

- [ ] **Step 1: Reset presentation loop state on track load**

In `library_load_event_cb()`, there are two existing success paths that set:

```c
        s_loop_active = false;
```

and:

```c
    s_loop_active = false;
```

Immediately after each one, add:

```c
        s_loop_active_beats = 0;
        ui_update_loop_screen_state();
```

Use indentation that matches the surrounding branch. This ensures a newly loaded track does not leave an old loop button highlighted.

- [ ] **Step 2: Update hot-cue callback loop presentation**

In `hot_cue_event_cb()`, after the hot-loop branch sets:

```c
        s_loop_active = true;
```

insert:

```c
        s_loop_active_beats = 0;
        ui_update_loop_screen_state();
```

In the normal-cue branch, after:

```c
        s_loop_active = false;
```

insert:

```c
        s_loop_active_beats = 0;
        ui_update_loop_screen_state();
```

Hot loops use `ACTIVE LOOP` because they may not map to one of the fixed beat-loop buttons.

- [ ] **Step 3: Update loop button callback active state**

In `loop_btn_event_cb()`, after:

```c
    s_loop_active = true;
```

insert:

```c
    s_loop_active_beats = beats;
    ui_update_loop_screen_state();
```

This highlights the tapped loop button and updates the status label.

- [ ] **Step 4: Update exit loop callback active state**

In `exit_loop_event_cb()`, after:

```c
    s_loop_active = false;
```

insert:

```c
    s_loop_active_beats = 0;
    ui_update_loop_screen_state();
```

Pressing Exit Loop while no loop is active remains harmless.

- [ ] **Step 5: Add Loop status label**

In `create_screen_beat_loop()`, after setting `s_screens[3]` size and position, insert:

```c
    s_label_loop_status = lv_label_create(s_screens[3]);
    ui_label_set_small_caps(s_label_loop_status, "NO ACTIVE LOOP", COL_TEXT_DIM);
    lv_obj_align(s_label_loop_status, LV_ALIGN_TOP_MID, 0, 12);
```

This creates the stable top status required by the spec.

- [ ] **Step 6: Move the Loop grid down slightly**

In `create_screen_beat_loop()`, replace:

```c
    int offset_y = 40;
```

with:

```c
    int offset_y = 54;
```

This creates space for the status label while preserving button size and spacing.

- [ ] **Step 7: Store loop button handles and use secondary styling**

In the Loop button creation loop, replace:

```c
        lv_obj_t *btn_loop = lv_button_create(s_screens[3]);
        lv_obj_add_style(btn_loop, &s_style_pressed, LV_STATE_PRESSED);
        lv_obj_set_size(btn_loop, pad_w, pad_h);
        lv_obj_set_pos(btn_loop, offset_x + col * (pad_w + spacing_x), offset_y + row * (pad_h + spacing_y));

        lv_obj_set_style_bg_color(btn_loop, COL_ACCENT, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(btn_loop, LV_OPA_20, LV_PART_MAIN);
        lv_obj_set_style_border_color(btn_loop, COL_ACCENT, LV_PART_MAIN);
        lv_obj_set_style_border_width(btn_loop, 2, LV_PART_MAIN);
        lv_obj_set_style_radius(btn_loop, 6, LV_PART_MAIN);

        lv_obj_set_user_data(btn_loop, (void*)(intptr_t)loop_beats[i]);
        lv_obj_add_event_cb(btn_loop, loop_btn_event_cb, LV_EVENT_CLICKED, NULL);

        lv_obj_t *lbl_loop = lv_label_create(btn_loop);
```

with:

```c
        s_loop_buttons[i] = lv_button_create(s_screens[3]);
        lv_obj_remove_style_all(s_loop_buttons[i]);
        lv_obj_add_style(s_loop_buttons[i], &s_style_btn_secondary, LV_PART_MAIN);
        lv_obj_add_style(s_loop_buttons[i], &s_style_pressed, LV_STATE_PRESSED);
        lv_obj_set_size(s_loop_buttons[i], pad_w, pad_h);
        lv_obj_set_pos(s_loop_buttons[i], offset_x + col * (pad_w + spacing_x), offset_y + row * (pad_h + spacing_y));

        lv_obj_set_user_data(s_loop_buttons[i], (void*)(intptr_t)loop_beats[i]);
        lv_obj_add_event_cb(s_loop_buttons[i], loop_btn_event_cb, LV_EVENT_CLICKED, NULL);

        lv_obj_t *lbl_loop = lv_label_create(s_loop_buttons[i]);
```

Keep label text and center alignment unchanged. `ui_update_loop_screen_state()` will apply active/inactive colors after creation.

- [ ] **Step 8: Style Exit Loop as destructive action**

In `create_screen_beat_loop()`, replace the Exit Loop styling:

```c
    lv_obj_t *btn_exit = lv_button_create(s_screens[3]);
    lv_obj_add_style(btn_exit, &s_style_pressed, LV_STATE_PRESSED);
    lv_obj_remove_style_all(btn_exit);
    lv_obj_add_style(btn_exit, &s_style_btn_neon, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn_exit, lv_color_hex(0xFF1744), LV_PART_MAIN); // Red color for Exit
```

with:

```c
    lv_obj_t *btn_exit = lv_button_create(s_screens[3]);
    lv_obj_remove_style_all(btn_exit);
    lv_obj_set_style_bg_color(btn_exit, COL_RED, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_exit, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn_exit, lv_color_hex(0xFF6B85), LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_exit, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(btn_exit, 6, LV_PART_MAIN);
    lv_obj_add_style(btn_exit, &s_style_pressed, LV_STATE_PRESSED);
```

Keep size `180x50`, position `(310,290)`, event callback, label text and label alignment unchanged.

- [ ] **Step 9: Initialize Loop screen state after creation**

At the end of `create_screen_beat_loop()`, after aligning `lbl_exit`, insert:

```c
    ui_update_loop_screen_state();
```

This sets the initial inactive status and inactive button colors after all handles exist.

- [ ] **Step 10: Build Loop polish**

Run:

```powershell
cd D:\AI\CDJ-XXX\repo\firmware\main-deck-p4
$activate = & "C:\Espressif\python_env\idf5.5_py3.13_env\Scripts\python.exe" "C:\Espressif\.espressif\v5.5.4\esp-idf\tools\activate.py" --export | Select-Object -First 1; . $activate; idf.py build
```

Expected result: build exits `0`, no `ui.c` warnings.

- [ ] **Step 11: Commit Loop polish**

Run:

```powershell
cd D:\AI\CDJ-XXX\repo
git add firmware/main-deck-p4/components/ui/ui.c
git commit -m "ui: polish loop active state"
```

Expected result: one commit containing only Loop screen and loop presentation-state changes.

---

### Task 4: Documentation And Final Verification

**Files:**
- Modify: `docs/development-plan.md`

- [ ] **Step 1: Update UI polish checklist**

In `docs/development-plan.md`, under the existing UI polish checklist, after the Header item, add:

```markdown
    - [x] Hot Cues + Loop: retained separate focused screens; cue pads now use semantic
      green/amber/dim states, Loop shows active/inactive status, and Exit Loop uses the
      shared destructive visual language.
```

- [ ] **Step 2: Run patch hygiene check**

Run:

```powershell
cd D:\AI\CDJ-XXX\repo
git diff --check
```

Expected result: no output and exit code `0`.

- [ ] **Step 3: Run final firmware build**

Run:

```powershell
cd D:\AI\CDJ-XXX\repo\firmware\main-deck-p4
$activate = & "C:\Espressif\python_env\idf5.5_py3.13_env\Scripts\python.exe" "C:\Espressif\.espressif\v5.5.4\esp-idf\tools\activate.py" --export | Select-Object -First 1; . $activate; idf.py build
```

Expected result: exit code `0`, output includes `Project build complete`, and no warnings mention `components/ui/ui.c`.

- [ ] **Step 4: Inspect changed files**

Run:

```powershell
cd D:\AI\CDJ-XXX\repo
git status --short --branch
git diff --stat HEAD~3..HEAD
```

Expected result: only `firmware/main-deck-p4/components/ui/ui.c` and `docs/development-plan.md` changed across the three implementation commits.

- [ ] **Step 5: Commit documentation update**

Run:

```powershell
cd D:\AI\CDJ-XXX\repo
git add docs/development-plan.md
git commit -m "docs: mark hot cues loop polish complete"
```

Expected result: one docs commit after the verified implementation commits.

- [ ] **Step 6: Push after user approval**

Run only after the user asks to push:

```powershell
cd D:\AI\CDJ-XXX\repo
git push origin main
```

Expected result: `main` is updated on `origin`.

---

## Self-Review

- Spec coverage: Task 2 implements semantic cue, loop and empty pad states. Task 3 implements separate Loop screen polish, active/inactive loop status and destructive Exit Loop styling. Task 4 updates project tracking and verifies build health.
- Behavioral boundary: The plan keeps `hot_cue_event_cb`, `loop_btn_event_cb` and `exit_loop_event_cb` as the behavior entry points. It only adds presentation refresh calls and a presentation-only `s_loop_active_beats` mirror.
- Layout boundary: Hot Cue button geometry remains unchanged. Loop buttons keep their size and spacing and move down only 14 px to fit the new status label. Exit Loop position remains unchanged.
- Type consistency: All helper names used later are defined in Task 1, and all new handles are guarded for `NULL` before use.
