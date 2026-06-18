# UI Polish Settings Implementation Plan

> Superseded note, 2026-06-18: the Settings `link_mode` control,
> `s_label_link_mode`, `link_mode_event_cb()`, and saved Wi-Fi role cycle were
> removed from active DDJ-FFL4 firmware. P4 now starts the hosted web UI/captive
> portal AP directly. Treat CDJ Link role-control tasks in this historical plan
> as superseded.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rebuild the `SETTINGS` tab as a two-column operational screen with controls on the left and live status on the right, without changing saved settings or runtime behavior.

**Architecture:** Keep the implementation inside `firmware/main-deck-p4/components/ui/ui.c`. Add small presentation helpers for Settings section headers/status labels, then replace only `create_screen_settings()` layout. Existing callbacks and object handles remain the behavior boundary.

**Tech Stack:** ESP-IDF C firmware, LVGL, existing `components/ui` styles and `COL_*` theme tokens, PowerShell, `idf.py build`.

---

## Files

- Modify: `firmware/main-deck-p4/components/ui/ui.c`
  - Add local Settings presentation helpers.
  - Replace the current single-column Settings layout with the approved two-column layout.
  - Preserve `s_slider_backlight`, `s_label_brightness_val`, `s_label_uart_status`, `s_label_audio_out`, `s_label_link_status`, and `s_label_link_mode`.
  - Preserve `slider_brightness_event_cb`, `audio_out_event_cb`, `link_mode_event_cb`, and `ui_update_link_status_label()`.
- Modify: `docs/development-plan.md`
  - Mark Settings polish complete under the UI polish checklist after implementation and verification.

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

### Task 1: Add Settings Presentation Helpers

**Files:**
- Modify: `firmware/main-deck-p4/components/ui/ui.c`

- [ ] **Step 1: Add a Settings section factory**

After `ui_update_loop_screen_state()` and before `ui_media_count()`, insert:

```c
static lv_obj_t *ui_settings_section(lv_obj_t *parent, int x, int y, int w, int h, const char *title)
{
    lv_obj_t *section = lv_obj_create(parent);
    lv_obj_remove_style_all(section);
    lv_obj_add_style(section, &s_style_panel_frame, LV_PART_MAIN);
    lv_obj_set_size(section, w, h);
    lv_obj_set_pos(section, x, y);
    lv_obj_clear_flag(section, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(section);
    ui_label_set_small_caps(label, title, COL_TEXT_MUTED);
    lv_obj_set_pos(label, 14, 12);

    return section;
}
```

This helper creates the left-column control sections and the right-column status surface.

- [ ] **Step 2: Add a value label helper**

Immediately after `ui_settings_section()`, insert:

```c
static lv_obj_t *ui_settings_value_label(lv_obj_t *parent, const char *text, lv_color_t color,
                                         const lv_font_t *font, int x, int y)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
    lv_obj_set_pos(label, x, y);
    return label;
}
```

This keeps status/value label setup compact inside `create_screen_settings()`.

- [ ] **Step 3: Build after helper insertion**

Run:

```powershell
cd D:\AI\CDJ-XXX\repo\firmware\main-deck-p4
$activate = & "C:\Espressif\python_env\idf5.5_py3.13_env\Scripts\python.exe" "C:\Espressif\.espressif\v5.5.4\esp-idf\tools\activate.py" --export | Select-Object -First 1; . $activate; idf.py build
```

Expected result: build exits `0`, no `ui.c` unused-function warnings. If a pure helper-only patch creates unused warnings, include the first minimal use in `create_screen_settings()` and report `DONE_WITH_CONCERNS`.

- [ ] **Step 4: Commit helper base**

Run:

```powershell
cd D:\AI\CDJ-XXX\repo
git add firmware/main-deck-p4/components/ui/ui.c
git commit -m "ui: add settings polish helpers"
```

Expected result: one commit containing only helper scaffolding or the minimum helper use needed to keep the build warning-free.

---

### Task 2: Rebuild Settings Control Column

**Files:**
- Modify: `firmware/main-deck-p4/components/ui/ui.c`

- [ ] **Step 1: Replace the old Settings top controls with left-column sections**

Inside `create_screen_settings()`, keep the screen creation and saved settings block unchanged:

```c
    s_screens[6] = lv_obj_create(parent);
    lv_obj_remove_style_all(s_screens[6]);
    lv_obj_add_style(s_screens[6], &s_style_screen_bg, LV_PART_MAIN);
    lv_obj_set_size(s_screens[6], 800, 370);
    lv_obj_set_pos(s_screens[6], 0, 55);

    // Saved settings (firmware); the simulator uses defaults.
#ifndef WIN32
    app_settings_t cfg = app_settings_get();
    int  bl_init  = cfg.backlight_pct;
    bool rca_init = (cfg.audio_out != 0);
#else
    int  bl_init  = 80;
    bool rca_init = false;
#endif
```

Delete the old direct child labels/controls from `// ─── Slider pozadinskog osvjetljenja (Gore) ───` through the CDJ Link button block, stopping before the old status panel comment.

Insert:

```c
    const int left_x = 30;
    const int left_w = 350;

    lv_obj_t *display_section = ui_settings_section(s_screens[6], left_x, 20, left_w, 86, "DISPLAY");
    s_slider_backlight = lv_slider_create(display_section);
    lv_obj_set_size(s_slider_backlight, 230, 18);
    lv_obj_set_pos(s_slider_backlight, 16, 48);
    lv_slider_set_range(s_slider_backlight, 10, 100);
    lv_slider_set_value(s_slider_backlight, bl_init, LV_ANIM_OFF);
    lv_obj_add_event_cb(s_slider_backlight, slider_brightness_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_label_brightness_val = ui_settings_value_label(display_section, "", COL_TEXT,
                                                     &lv_font_montserrat_14, 270, 44);
    lv_label_set_text_fmt(s_label_brightness_val, "%d%%", bl_init);

    lv_obj_t *audio_section = ui_settings_section(s_screens[6], left_x, 118, left_w, 86, "AUDIO OUTPUT");
    lv_obj_t *sw_audio = lv_switch_create(audio_section);
    lv_obj_set_pos(sw_audio, 16, 42);
    lv_obj_add_event_cb(sw_audio, audio_out_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_label_audio_out = ui_settings_value_label(audio_section, rca_init ? "RCA LINE-OUT" : "SPEAKER",
                                                COL_GREEN, &lv_font_montserrat_16, 104, 44);
    if (rca_init) {
        lv_obj_add_state(sw_audio, LV_STATE_CHECKED);
    }

    lv_obj_t *link_section = ui_settings_section(s_screens[6], left_x, 216, left_w, 116, "CDJ LINK ROLE");
    lv_obj_t *btn_link = lv_button_create(link_section);
    lv_obj_remove_style_all(btn_link);
    lv_obj_add_style(btn_link, &s_style_btn_secondary, LV_PART_MAIN);
    lv_obj_add_style(btn_link, &s_style_pressed, LV_STATE_PRESSED);
    lv_obj_set_size(btn_link, 210, 42);
    lv_obj_set_pos(btn_link, 16, 42);
#ifndef WIN32
    lv_obj_add_event_cb(btn_link, link_mode_event_cb, LV_EVENT_CLICKED, NULL);
#endif

    s_label_link_mode = lv_label_create(btn_link);
#ifndef WIN32
    lv_label_set_text_fmt(s_label_link_mode, "%s", ui_link_mode_name(cfg.link_mode));
#else
    lv_label_set_text(s_label_link_mode, "OFF");
#endif
    lv_obj_set_style_text_font(s_label_link_mode, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_label_link_mode, COL_TEXT, LV_PART_MAIN);
    lv_obj_align(s_label_link_mode, LV_ALIGN_CENTER, 0, 0);

    ui_settings_value_label(link_section, "Saved role applies after reboot", COL_AMBER,
                            &lv_font_montserrat_12, 16, 90);
```

This keeps all existing callbacks but changes the Link button text from `LINK MODE: HOST USB` to the tighter `HOST USB`, because the section header already provides context.

- [ ] **Step 2: Keep `link_mode_event_cb()` text consistent with the new button**

In `link_mode_event_cb()`, replace:

```c
        lv_label_set_text_fmt(s_label_link_mode, "LINK MODE: %s", ui_link_mode_name(next));
```

with:

```c
        lv_label_set_text_fmt(s_label_link_mode, "%s", ui_link_mode_name(next));
```

Leave `s_label_link_status` and the saved-mode log unchanged.

- [ ] **Step 3: Build control column polish**

Run:

```powershell
cd D:\AI\CDJ-XXX\repo\firmware\main-deck-p4
$activate = & "C:\Espressif\python_env\idf5.5_py3.13_env\Scripts\python.exe" "C:\Espressif\.espressif\v5.5.4\esp-idf\tools\activate.py" --export | Select-Object -First 1; . $activate; idf.py build
```

Expected result: build exits `0`, no `ui.c` warnings.

- [ ] **Step 4: Commit control column**

Run:

```powershell
cd D:\AI\CDJ-XXX\repo
git add firmware/main-deck-p4/components/ui/ui.c
git commit -m "ui: polish settings control column"
```

Expected result: one commit containing the left-column Settings controls and link-mode label adjustment.

---

### Task 3: Rebuild Settings Status Column

**Files:**
- Modify: `firmware/main-deck-p4/components/ui/ui.c`

- [ ] **Step 1: Replace the old bottom status panel with a right-column status surface**

In `create_screen_settings()`, delete the old status panel block from:

```c
    // ─── Status panel (Dolje) ───
    lv_obj_t *info_card = lv_obj_create(s_screens[6]);
```

through the creation/alignment of `lbl_hw`.

Insert immediately after the new left-column CDJ Link role section:

```c
    lv_obj_t *status_section = ui_settings_section(s_screens[6], 410, 20, 360, 312, "SYSTEM STATUS");

    s_label_uart_status = ui_settings_value_label(status_section,
                                                  "Control Link (S3): Connected (Uptime: 42s)",
                                                  COL_GREEN, &lv_font_montserrat_12, 16, 46);

    s_label_link_status = ui_settings_value_label(status_section, "Link: OFF",
                                                  COL_ACCENT, &lv_font_montserrat_12, 16, 74);

    ui_settings_value_label(status_section, "USB Media Drive", COL_TEXT_MUTED,
                            &lv_font_montserrat_12, 16, 118);
    ui_settings_value_label(status_section, "14.8 GB / 32 GB free (FAT32)", COL_TEXT_DIM,
                            &lv_font_montserrat_12, 16, 142);

    ui_settings_value_label(status_section, "SD Cache", COL_TEXT_MUTED,
                            &lv_font_montserrat_12, 16, 178);
    ui_settings_value_label(status_section, "Active (1.2 MB / 8 MB)", COL_TEXT_DIM,
                            &lv_font_montserrat_12, 16, 202);

    ui_settings_value_label(status_section, "Board: JC4880P443C_I_W (ESP32-P4 N16R8)",
                            COL_TEXT_DIM, &lv_font_montserrat_12, 16, 252);
    ui_settings_value_label(status_section, "Firmware: Main Deck Engine v1.0.0-Beta (IDF v5.5)",
                            COL_TEXT_DIM, &lv_font_montserrat_12, 16, 276);
```

Keep the existing firmware-only live status call after the block:

```c
#ifndef WIN32
    ui_update_link_status_label();
#endif
```

- [ ] **Step 2: Ensure status labels can fit long runtime text**

After creating `s_label_uart_status`, add:

```c
    lv_obj_set_width(s_label_uart_status, 320);
    lv_label_set_long_mode(s_label_uart_status, LV_LABEL_LONG_CLIP);
```

After creating `s_label_link_status`, add:

```c
    lv_obj_set_width(s_label_link_status, 320);
    lv_label_set_long_mode(s_label_link_status, LV_LABEL_LONG_CLIP);
```

This prevents long peer names or uptime strings from overflowing into neighboring UI.

- [ ] **Step 3: Build status column polish**

Run:

```powershell
cd D:\AI\CDJ-XXX\repo\firmware\main-deck-p4
$activate = & "C:\Espressif\python_env\idf5.5_py3.13_env\Scripts\python.exe" "C:\Espressif\.espressif\v5.5.4\esp-idf\tools\activate.py" --export | Select-Object -First 1; . $activate; idf.py build
```

Expected result: build exits `0`, no `ui.c` warnings.

- [ ] **Step 4: Commit status column**

Run:

```powershell
cd D:\AI\CDJ-XXX\repo
git add firmware/main-deck-p4/components/ui/ui.c
git commit -m "ui: polish settings status column"
```

Expected result: one commit containing only right-column status layout and overflow protection.

---

### Task 4: Documentation And Final Verification

**Files:**
- Modify: `docs/development-plan.md`

- [ ] **Step 1: Update UI polish checklist**

In `docs/development-plan.md`, under the existing UI polish checklist, after the Hot Cues + Loop item, add:

```markdown
    - [x] Settings: reorganized into an operational two-column screen with
      controls on the left, live system/link status on the right, and clearer
      CDJ Link role/reboot messaging.
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
git diff --stat HEAD~2..HEAD
```

Expected result before the docs commit: the two implementation commits affect only `firmware/main-deck-p4/components/ui/ui.c`.

- [ ] **Step 5: Commit documentation update**

Run:

```powershell
cd D:\AI\CDJ-XXX\repo
git add docs/development-plan.md
git commit -m "docs: mark settings polish complete"
```

Expected result: one docs commit after verified implementation commits.

- [ ] **Step 6: Push only after user asks**

Run only after the user asks to push:

```powershell
cd D:\AI\CDJ-XXX\repo
git push origin main
```

Expected result: `main` is updated on `origin`.

---

## Self-Review

- Spec coverage: Tasks 2 and 3 implement the approved two-column Settings layout, separate controls from live status, preserve CDJ Link role/status behavior, and keep touch controls large.
- Behavioral boundary: Existing callbacks and settings APIs remain unchanged. Only `s_label_link_mode` display text is shortened because the section header now carries the context.
- Runtime data boundary: The plan restyles existing USB/SD/board/firmware text and does not claim new live storage monitoring.
- Type consistency: New helper names are defined in Task 1 and used consistently in Tasks 2 and 3. Existing object handles remain unchanged.
- Verification: Every implementation task runs `idf.py build`; final task also runs `git diff --check`.
