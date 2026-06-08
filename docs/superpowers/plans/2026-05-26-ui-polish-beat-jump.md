# UI Polish Beat Jump Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Polish the `BEAT JUMP` tab into two clear direction lanes while preserving all existing jump behavior.

**Architecture:** Keep the implementation inside `firmware/main-deck-p4/components/ui/ui.c`. Add one small presentation helper for Beat Jump buttons, then rebuild only `create_screen_beat_jump()` layout. Existing `jump_btn_event_cb()` and positive/negative user data values remain the behavior boundary.

**Tech Stack:** ESP-IDF C firmware, LVGL, existing `components/ui` styles and `COL_*` theme tokens, PowerShell, `idf.py build`.

---

## Files

- Modify: `firmware/main-deck-p4/components/ui/ui.c`
  - Add one local Beat Jump button helper.
  - Replace the current sentence-header + two loop blocks in `create_screen_beat_jump()` with two labelled lanes.
  - Preserve `jump_btn_event_cb()`.
  - Preserve jump values: `-1`, `-4`, `-8`, `-16`, `+1`, `+4`, `+8`, `+16`.
- Modify: `docs/development-plan.md`
  - Mark Beat Jump polish complete under the UI polish checklist after implementation and verification.

## Validation Commands

Run from `D:\AI\CDJ-XXX\repo` unless specified otherwise.

Patch hygiene:

```powershell
git diff --check
```

Expected result: no output and exit code `0`.

Firmware build:

```powershell
cd D:\AI\CDJ-XXX\repo\firmware\main-deck-p4
$activate = & "C:\Espressif\python_env\idf5.5_py3.13_env\Scripts\python.exe" "C:\Espressif\.espressif\v5.5.4\esp-idf\tools\activate.py" --export | Select-Object -First 1; . $activate; idf.py build
```

Expected result: exit code `0`, output includes `Project build complete`, and no warnings mention `components/ui/ui.c`.

---

### Task 1: Add Beat Jump Button Helper

**Files:**
- Modify: `firmware/main-deck-p4/components/ui/ui.c`

- [ ] **Step 1: Add the helper**

After `ui_settings_value_label()` and before `ui_media_count()`, insert:

```c
static lv_obj_t *ui_create_beat_jump_button(lv_obj_t *parent, int x, int y, int value,
                                            bool forward)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_add_style(btn, &s_style_pressed, LV_STATE_PRESSED);
    lv_obj_set_size(btn, 150, 82);
    lv_obj_set_pos(btn, x, y);

    lv_color_t accent = forward ? COL_GREEN : COL_RED;
    lv_color_t fill = forward ? lv_color_hex(0x10251B) : lv_color_hex(0x2A1016);
    lv_obj_set_style_bg_color(btn, fill, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, accent, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 6, LV_PART_MAIN);

    lv_obj_set_user_data(btn, (void*)(intptr_t)value);
    lv_obj_add_event_cb(btn, jump_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl = lv_label_create(btn);
    int abs_value = value < 0 ? -value : value;
    lv_label_set_text_fmt(lbl, "%c%d BEAT%s", forward ? '+' : '-', abs_value,
                          abs_value == 1 ? "" : "S");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, COL_TEXT, LV_PART_MAIN);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);

    return btn;
}
```

This helper is presentation-only. It keeps the existing event callback and stores the signed jump value in user data.

- [ ] **Step 2: Avoid unused-helper warnings**

Do not build or commit until Task 2 uses `ui_create_beat_jump_button()`. A helper-only build will create an unused static function warning.

---

### Task 2: Rebuild Beat Jump Direction Lanes

**Files:**
- Modify: `firmware/main-deck-p4/components/ui/ui.c`

- [ ] **Step 1: Replace the current Beat Jump content**

Inside `create_screen_beat_jump()`, keep the screen object creation unchanged:

```c
    s_screens[4] = lv_obj_create(parent);
    lv_obj_remove_style_all(s_screens[4]);
    lv_obj_add_style(s_screens[4], &s_style_screen_bg, LV_PART_MAIN);
    lv_obj_set_size(s_screens[4], 800, 370);
    lv_obj_set_pos(s_screens[4], 0, 55);
```

Delete everything after that screen setup through the end of the second button loop.

Insert:

```c
    int jump_vals[4] = {1, 4, 8, 16};
    const int lane_x = 40;
    const int lane_w = 720;
    const int lane_h = 132;
    const int lane_gap = 24;
    const int lane_top_y = 34;
    const int btn_w = 150;
    const int btn_h = 82;
    const int spacing_x = 24;
    const int btn_x0 = 58;
    const int btn_y = 38;

    lv_obj_t *lane_back = lv_obj_create(s_screens[4]);
    lv_obj_remove_style_all(lane_back);
    lv_obj_add_style(lane_back, &s_style_panel_frame, LV_PART_MAIN);
    lv_obj_set_size(lane_back, lane_w, lane_h);
    lv_obj_set_pos(lane_back, lane_x, lane_top_y);
    lv_obj_clear_flag(lane_back, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_back = lv_label_create(lane_back);
    ui_label_set_small_caps(lbl_back, "BACKWARD", COL_AMBER);
    lv_obj_set_pos(lbl_back, 16, 12);

    for (int i = 0; i < 4; i++) {
        ui_create_beat_jump_button(lane_back, btn_x0 + i * (btn_w + spacing_x), btn_y,
                                   -jump_vals[i], false);
    }

    lv_obj_t *lane_forward = lv_obj_create(s_screens[4]);
    lv_obj_remove_style_all(lane_forward);
    lv_obj_add_style(lane_forward, &s_style_panel_frame, LV_PART_MAIN);
    lv_obj_set_size(lane_forward, lane_w, lane_h);
    lv_obj_set_pos(lane_forward, lane_x, lane_top_y + lane_h + lane_gap);
    lv_obj_clear_flag(lane_forward, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_forward = lv_label_create(lane_forward);
    ui_label_set_small_caps(lbl_forward, "FORWARD", COL_GREEN);
    lv_obj_set_pos(lbl_forward, 16, 12);

    for (int i = 0; i < 4; i++) {
        ui_create_beat_jump_button(lane_forward, btn_x0 + i * (btn_w + spacing_x), btn_y,
                                   jump_vals[i], true);
    }
```

The local `btn_h` constant is intentionally present next to `btn_w` to document the fixed button geometry. If the compiler warns that it is unused, remove `btn_h` because the helper owns the final button height.

- [ ] **Step 2: Remove any unused constants**

If `idf.py build` reports an unused local variable for `btn_h`, remove this line from `create_screen_beat_jump()`:

```c
    const int btn_h = 82;
```

Do not change button height elsewhere; `ui_create_beat_jump_button()` already sets `150x82`.

- [ ] **Step 3: Build Beat Jump polish**

Run:

```powershell
cd D:\AI\CDJ-XXX\repo\firmware\main-deck-p4
$activate = & "C:\Espressif\python_env\idf5.5_py3.13_env\Scripts\python.exe" "C:\Espressif\.espressif\v5.5.4\esp-idf\tools\activate.py" --export | Select-Object -First 1; . $activate; idf.py build
```

Expected result: build exits `0`, output includes `Project build complete`, and no warnings mention `components/ui/ui.c`.

- [ ] **Step 4: Commit Beat Jump polish**

Run:

```powershell
cd D:\AI\CDJ-XXX\repo
git add firmware/main-deck-p4/components/ui/ui.c
git commit -m "ui: polish beat jump lanes"
```

Expected result: one commit containing only the Beat Jump UI helper and lane layout.

---

### Task 3: Documentation And Final Verification

**Files:**
- Modify: `docs/development-plan.md`

- [ ] **Step 1: Update UI polish checklist**

In `docs/development-plan.md`, under the existing UI polish checklist, after the Settings item, add:

```markdown
    - [x] Beat Jump: retained the dedicated screen and existing jump values while
      replacing the explanatory header with clear backward/forward lanes and
      restrained red/green performance buttons.
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
git diff --stat HEAD~1..HEAD
```

Expected result before the docs commit: the implementation commit affects only `firmware/main-deck-p4/components/ui/ui.c`.

- [ ] **Step 5: Commit documentation update**

Run:

```powershell
cd D:\AI\CDJ-XXX\repo
git add docs/development-plan.md
git commit -m "docs: mark beat jump polish complete"
```

Expected result: one docs commit after verified implementation.

- [ ] **Step 6: Push only after user asks**

Run only after the user asks to push:

```powershell
cd D:\AI\CDJ-XXX\repo
git push origin main
```

Expected result: `main` is updated on `origin`.

---

## Self-Review

- Spec coverage: Task 2 implements the approved two-lane Beat Jump design, removes the explanatory header, keeps large buttons, and preserves the signed jump values.
- Behavioral boundary: `jump_btn_event_cb()` is not changed. User data remains signed exactly as before.
- Runtime boundary: No changes to `deck_core`, `audio_engine`, beatgrid parsing, media, USB, SD or CDJ Link.
- Type consistency: `ui_create_beat_jump_button()` is defined in Task 1 and used in Task 2.
- Verification: Task 2 and Task 3 run full firmware builds, and Task 3 runs `git diff --check`.
