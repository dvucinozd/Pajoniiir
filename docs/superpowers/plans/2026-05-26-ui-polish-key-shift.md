# UI Polish Key Shift Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Polish the `KEY SHIFT` tab into two clear control/readout panels while preserving the existing keylock switch behavior.

**Architecture:** Keep the implementation inside `firmware/main-deck-p4/components/ui/ui.c`. Replace only the presentation content of `create_screen_key_shift()` with two framed LVGL panels that reuse existing theme tokens and helper styles. Leave `keylock_toggle_event_cb()` and media/audio behavior untouched.

**Tech Stack:** ESP-IDF C firmware, LVGL, existing `components/ui` styles and `COL_*` theme tokens, PowerShell, `idf.py build`.

---

## Files

- Modify: `firmware/main-deck-p4/components/ui/ui.c`
  - Replace the current `KEY SHIFT` card layout inside `create_screen_key_shift()`.
  - Reuse `s_style_panel_frame` for both panels.
  - Reuse `ui_label_set_small_caps()` for section labels.
  - Preserve the existing `lv_switch_create()` and `keylock_toggle_event_cb()` wiring.
- Modify: `docs/development-plan.md`
  - Mark Key Shift polish complete under the UI polish checklist after implementation and verification.

## Validation Commands

Run from `D:\AI\CDJ-XXX\repo` unless the command changes directory.

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

### Task 1: Rebuild Key Shift Panels

**Files:**
- Modify: `firmware/main-deck-p4/components/ui/ui.c`

- [ ] **Step 1: Replace the current Key Shift content**

Inside `create_screen_key_shift()`, keep the screen object creation unchanged:

```c
    s_screens[5] = lv_obj_create(parent);
    lv_obj_remove_style_all(s_screens[5]);
    lv_obj_add_style(s_screens[5], &s_style_screen_bg, LV_PART_MAIN);
    lv_obj_set_size(s_screens[5], 800, 370);
    lv_obj_set_pos(s_screens[5], 0, 55);
```

Delete everything after that setup through the current `lbl_val` creation. Insert:

```c
    const int panel_y = 40;
    const int panel_h = 250;

    lv_obj_t *tempo_panel = lv_obj_create(s_screens[5]);
    lv_obj_remove_style_all(tempo_panel);
    lv_obj_add_style(tempo_panel, &s_style_panel_frame, LV_PART_MAIN);
    lv_obj_set_size(tempo_panel, 320, panel_h);
    lv_obj_set_pos(tempo_panel, 50, panel_y);
    lv_obj_clear_flag(tempo_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_tempo = lv_label_create(tempo_panel);
    ui_label_set_small_caps(lbl_tempo, "MASTER TEMPO", COL_TEXT_MUTED);
    lv_obj_set_pos(lbl_tempo, 18, 16);

    lv_obj_t *lbl_keylock = lv_label_create(tempo_panel);
    lv_label_set_text(lbl_keylock, "KEY LOCK");
    lv_obj_set_style_text_font(lbl_keylock, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_keylock, COL_GREEN, LV_PART_MAIN);
    lv_obj_align(lbl_keylock, LV_ALIGN_TOP_LEFT, 18, 64);

    lv_obj_t *sw_keylock = lv_switch_create(tempo_panel);
    lv_obj_set_pos(sw_keylock, 18, 130);
    lv_obj_add_event_cb(sw_keylock, keylock_toggle_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *lbl_preserves = lv_label_create(tempo_panel);
    lv_label_set_text(lbl_preserves, "PRESERVES KEY");
    lv_obj_set_style_text_font(lbl_preserves, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_preserves, COL_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_pos(lbl_preserves, 18, 196);

    lv_obj_t *transpose_panel = lv_obj_create(s_screens[5]);
    lv_obj_remove_style_all(transpose_panel);
    lv_obj_add_style(transpose_panel, &s_style_panel_frame, LV_PART_MAIN);
    lv_obj_set_size(transpose_panel, 360, panel_h);
    lv_obj_set_pos(transpose_panel, 410, panel_y);
    lv_obj_clear_flag(transpose_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_transpose = lv_label_create(transpose_panel);
    ui_label_set_small_caps(lbl_transpose, "KEY TRANSPOSE", COL_TEXT_MUTED);
    lv_obj_set_pos(lbl_transpose, 18, 16);

    lv_obj_t *lbl_key_value = lv_label_create(transpose_panel);
    lv_label_set_text(lbl_key_value, "ORIGINAL KEY");
    lv_obj_set_style_text_font(lbl_key_value, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_key_value, COL_TEXT, LV_PART_MAIN);
    lv_obj_align(lbl_key_value, LV_ALIGN_TOP_LEFT, 18, 84);

    lv_obj_t *lbl_no_transpose = lv_label_create(transpose_panel);
    lv_label_set_text(lbl_no_transpose, "NO TRANSPOSITION");
    lv_obj_set_style_text_font(lbl_no_transpose, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_no_transpose, COL_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_pos(lbl_no_transpose, 18, 150);
```

This keeps the existing switch callback line exactly in the new layout:

```c
    lv_obj_add_event_cb(sw_keylock, keylock_toggle_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
```

- [ ] **Step 2: Verify the callback body is unchanged**

Confirm `keylock_toggle_event_cb()` still matches this behavior boundary:

```c
static void keylock_toggle_event_cb(lv_event_t *e) {
#ifdef WIN32
    mock_deck_toggle_master_tempo();
    deck_state_t state = deck_core_get_state();
    ESP_LOGI(TAG, "Master Tempo toggled: %s", state.master_tempo ? "ON" : "OFF");
#endif
}
```

Do not add firmware-side state changes in this polish pass.

- [ ] **Step 3: Build Key Shift polish**

Run:

```powershell
cd D:\AI\CDJ-XXX\repo\firmware\main-deck-p4
$activate = & "C:\Espressif\python_env\idf5.5_py3.13_env\Scripts\python.exe" "C:\Espressif\.espressif\v5.5.4\esp-idf\tools\activate.py" --export | Select-Object -First 1; . $activate; idf.py build
```

Expected result: build exits `0`, output includes `Project build complete`, and no warnings mention `components/ui/ui.c`.

- [ ] **Step 4: Commit Key Shift UI polish**

Run:

```powershell
cd D:\AI\CDJ-XXX\repo
git add firmware/main-deck-p4/components/ui/ui.c
git commit -m "ui: polish key shift panels"
```

Expected result: one commit containing only the `create_screen_key_shift()` layout change.

---

### Task 2: Documentation And Final Verification

**Files:**
- Modify: `docs/development-plan.md`

- [ ] **Step 1: Update UI polish checklist**

In `docs/development-plan.md`, under the existing UI polish checklist, after the Beat Jump item, add:

```markdown
    - [x] Key Shift: retained the dedicated screen and existing keylock switch
      behavior while restyling Master Tempo and Key Transpose as clear
      two-panel control/readout surfaces.
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

Expected result: build exits `0`, output includes `Project build complete`, and no warnings mention `components/ui/ui.c`.

- [ ] **Step 4: Commit documentation update**

Run:

```powershell
cd D:\AI\CDJ-XXX\repo
git add docs/development-plan.md
git commit -m "docs: mark key shift polish complete"
```

Expected result: one documentation-only commit.

---

## Manual Visual Check

After the firmware build, inspect on simulator or hardware when practical:

- `MASTER TEMPO`, `KEY LOCK`, `PRESERVES KEY`, `KEY TRANSPOSE`, `ORIGINAL KEY`, and `NO TRANSPOSITION` fit inside the two panels at 800x480.
- The keylock switch remains reachable by touch.
- The transpose panel reads as a display-only state, with no new controls.

## Self-Review

- Spec coverage: Task 1 implements the two-panel option A layout, preserves the callback, reuses existing style helpers, and avoids new audio/key processing. Task 2 records the completed polish in the project plan.
- Type consistency: The plan uses existing LVGL APIs, existing `COL_*` tokens, existing `s_style_panel_frame`, and existing `ui_label_set_small_caps()`.
- Verification coverage: The plan includes `git diff --check`, firmware build, compiler warning review, and manual 800x480 visual checks.
