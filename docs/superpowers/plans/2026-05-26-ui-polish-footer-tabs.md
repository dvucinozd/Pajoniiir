# UI Polish Footer Tabs Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Polish the global footer navigation into a clearer seven-tab performance bar while preserving existing tab switching behavior.

**Architecture:** Keep the implementation inside `firmware/main-deck-p4/components/ui/ui.c`. Add one presentational active-strip array, refine existing footer/tab styles, define a disabled tab style for future use, and update `create_footer()` plus `footer_btn_event_cb()` to show the active strip without changing navigation logic.

**Tech Stack:** ESP-IDF C firmware, LVGL, existing `components/ui` styles and `COL_*` theme tokens, PowerShell, `idf.py build`.

---

## Files

- Modify: `firmware/main-deck-p4/components/ui/ui.c`
  - Add `s_footer_active_strips[7]`.
  - Add `s_style_tab_btn_disabled`.
  - Refine `s_style_footer`, `s_style_tab_btn_normal` and `s_style_tab_btn_active`.
  - Update `footer_btn_event_cb()` to toggle active strip visibility while keeping screen switching intact.
  - Update `create_footer()` to create fixed-size tab buttons, centered labels and hidden active strips.
- Modify: `docs/development-plan.md`
  - Mark footer/tab navigation polish complete under the UI polish checklist after implementation and verification.

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

### Task 1: Add Footer Presentation State

**Files:**
- Modify: `firmware/main-deck-p4/components/ui/ui.c`

- [ ] **Step 1: Add active-strip storage**

Near the existing footer globals:

```c
// Footer navigation buttons
static lv_obj_t *s_footer_buttons[7];
static const char *s_tab_names[7] = {
    "OVERVIEW", "LIBRARY", "HOT CUES", "LOOP", "BEAT JUMP", "KEY SHIFT", "SETTINGS"
};
```

insert `s_footer_active_strips[7]` between the buttons and tab names:

```c
// Footer navigation buttons
static lv_obj_t *s_footer_buttons[7];
static lv_obj_t *s_footer_active_strips[7];
static const char *s_tab_names[7] = {
    "OVERVIEW", "LIBRARY", "HOT CUES", "LOOP", "BEAT JUMP", "KEY SHIFT", "SETTINGS"
};
```

- [ ] **Step 2: Add disabled tab style declaration**

Near the existing footer style declarations:

```c
static lv_style_t s_style_footer;
static lv_style_t s_style_tab_btn_normal;
static lv_style_t s_style_tab_btn_active;
static lv_style_t s_style_screen_bg;
```

insert:

```c
static lv_style_t s_style_tab_btn_disabled;
```

so the block becomes:

```c
static lv_style_t s_style_footer;
static lv_style_t s_style_tab_btn_normal;
static lv_style_t s_style_tab_btn_active;
static lv_style_t s_style_tab_btn_disabled;
static lv_style_t s_style_screen_bg;
```

Do not apply the disabled style to any current footer tab in this task.

---

### Task 2: Refine Footer Tab Styles

**Files:**
- Modify: `firmware/main-deck-p4/components/ui/ui.c`

- [ ] **Step 1: Replace footer and tab style initialization**

Inside the style initialization function, replace the current footer/tab style block:

```c
    // Footer bar style
    lv_style_init(&s_style_footer);
    lv_style_set_bg_color(&s_style_footer, COL_FOOTER);
    lv_style_set_bg_opa(&s_style_footer, LV_OPA_COVER);
    lv_style_set_border_width(&s_style_footer, 1);
    lv_style_set_border_color(&s_style_footer, COL_BORDER);
    lv_style_set_border_side(&s_style_footer, LV_BORDER_SIDE_TOP);
    lv_style_set_pad_all(&s_style_footer, 4);

    // Tab buttons - Normal
    lv_style_init(&s_style_tab_btn_normal);
    lv_style_set_bg_color(&s_style_tab_btn_normal, COL_SURFACE);
    lv_style_set_bg_opa(&s_style_tab_btn_normal, LV_OPA_COVER);
    lv_style_set_text_color(&s_style_tab_btn_normal, COL_TEXT_MUTED);
    lv_style_set_border_width(&s_style_tab_btn_normal, 1);
    lv_style_set_border_color(&s_style_tab_btn_normal, lv_color_hex(0x2A2A2A));
    lv_style_set_radius(&s_style_tab_btn_normal, 4);
    
    // Tab buttons - Active (Neon Accent)
    lv_style_init(&s_style_tab_btn_active);
    lv_style_set_bg_color(&s_style_tab_btn_active, COL_ACCENT_DK);
    lv_style_set_bg_opa(&s_style_tab_btn_active, LV_OPA_COVER);
    lv_style_set_text_color(&s_style_tab_btn_active, COL_TEXT);
    lv_style_set_border_width(&s_style_tab_btn_active, 1);
    lv_style_set_border_color(&s_style_tab_btn_active, COL_ACCENT);
    lv_style_set_radius(&s_style_tab_btn_active, 4);
```

with:

```c
    // Footer bar style
    lv_style_init(&s_style_footer);
    lv_style_set_bg_color(&s_style_footer, COL_FOOTER);
    lv_style_set_bg_opa(&s_style_footer, LV_OPA_COVER);
    lv_style_set_border_width(&s_style_footer, 1);
    lv_style_set_border_color(&s_style_footer, COL_BORDER);
    lv_style_set_border_side(&s_style_footer, LV_BORDER_SIDE_TOP);
    lv_style_set_pad_all(&s_style_footer, 0);

    // Tab buttons - Normal
    lv_style_init(&s_style_tab_btn_normal);
    lv_style_set_bg_color(&s_style_tab_btn_normal, COL_PANEL_DK);
    lv_style_set_bg_opa(&s_style_tab_btn_normal, LV_OPA_COVER);
    lv_style_set_text_color(&s_style_tab_btn_normal, COL_TEXT_MUTED);
    lv_style_set_border_width(&s_style_tab_btn_normal, 1);
    lv_style_set_border_color(&s_style_tab_btn_normal, COL_BORDER);
    lv_style_set_radius(&s_style_tab_btn_normal, 4);
    lv_style_set_pad_all(&s_style_tab_btn_normal, 0);

    // Tab buttons - Active
    lv_style_init(&s_style_tab_btn_active);
    lv_style_set_bg_color(&s_style_tab_btn_active, COL_ACCENT_DK);
    lv_style_set_bg_opa(&s_style_tab_btn_active, LV_OPA_COVER);
    lv_style_set_text_color(&s_style_tab_btn_active, COL_TEXT);
    lv_style_set_border_width(&s_style_tab_btn_active, 1);
    lv_style_set_border_color(&s_style_tab_btn_active, COL_ACCENT);
    lv_style_set_radius(&s_style_tab_btn_active, 4);
    lv_style_set_pad_all(&s_style_tab_btn_active, 0);

    // Tab buttons - Disabled (future use)
    lv_style_init(&s_style_tab_btn_disabled);
    lv_style_set_bg_color(&s_style_tab_btn_disabled, COL_SURFACE);
    lv_style_set_bg_opa(&s_style_tab_btn_disabled, LV_OPA_COVER);
    lv_style_set_text_color(&s_style_tab_btn_disabled, COL_TEXT_DIM);
    lv_style_set_border_width(&s_style_tab_btn_disabled, 1);
    lv_style_set_border_color(&s_style_tab_btn_disabled, lv_color_hex(0x242424));
    lv_style_set_radius(&s_style_tab_btn_disabled, 4);
    lv_style_set_pad_all(&s_style_tab_btn_disabled, 0);
```

This keeps the footer compact and adds the disabled visual language without changing runtime tab availability.

---

### Task 3: Add Active Strip To Footer Behavior

**Files:**
- Modify: `firmware/main-deck-p4/components/ui/ui.c`

- [ ] **Step 1: Update active strip visibility in the footer callback**

Inside `footer_btn_event_cb()`, replace the current loop:

```c
    // Update visibility of screens
    for (int i = 0; i < 7; i++) {
        if (i == target_idx) {
            lv_obj_remove_flag(s_screens[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_style(s_footer_buttons[i], &s_style_tab_btn_active, LV_PART_MAIN);
        } else {
            lv_obj_add_flag(s_screens[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_replace_style(s_footer_buttons[i], &s_style_tab_btn_active, &s_style_tab_btn_normal, LV_PART_MAIN);
        }
    }
```

with:

```c
    // Update visibility of screens
    for (int i = 0; i < 7; i++) {
        if (i == target_idx) {
            lv_obj_remove_flag(s_screens[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_style(s_footer_buttons[i], &s_style_tab_btn_active, LV_PART_MAIN);
            if (s_footer_active_strips[i]) {
                lv_obj_remove_flag(s_footer_active_strips[i], LV_OBJ_FLAG_HIDDEN);
            }
        } else {
            lv_obj_add_flag(s_screens[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_replace_style(s_footer_buttons[i], &s_style_tab_btn_active,
                                 &s_style_tab_btn_normal, LV_PART_MAIN);
            if (s_footer_active_strips[i]) {
                lv_obj_add_flag(s_footer_active_strips[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
```

This keeps the existing screen visibility and active style logic, adding only active-strip visibility.

- [ ] **Step 2: Update footer button construction**

Inside `create_footer()`, replace:

```c
    // We will place 7 navigation buttons in a grid row
    int btn_width = 106;
    int spacing = 6;
    int offset_left = 6;
```

with:

```c
    const int btn_width = 106;
    const int btn_height = 47;
    const int spacing = 6;
    const int offset_left = 6;
    const int offset_top = 4;
```

Then replace the body of the `for (int i = 0; i < 7; i++)` loop:

```c
        s_footer_buttons[i] = lv_button_create(s_footer_container);
        lv_obj_remove_style_all(s_footer_buttons[i]);
        lv_obj_add_style(s_footer_buttons[i], &s_style_tab_btn_normal, LV_PART_MAIN);
        lv_obj_add_style(s_footer_buttons[i], &s_style_pressed, LV_STATE_PRESSED);
        lv_obj_set_size(s_footer_buttons[i], btn_width, 45);
        lv_obj_set_pos(s_footer_buttons[i], offset_left + i * (btn_width + spacing), 2);
        
        lv_obj_set_user_data(s_footer_buttons[i], (void*)(intptr_t)i);
        lv_obj_add_event_cb(s_footer_buttons[i], footer_btn_event_cb, LV_EVENT_CLICKED, NULL);

        lv_obj_t *lbl = lv_label_create(s_footer_buttons[i]);
        lv_label_set_text(lbl, s_tab_names[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, LV_PART_MAIN);
        lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
```

with:

```c
        s_footer_buttons[i] = lv_button_create(s_footer_container);
        lv_obj_remove_style_all(s_footer_buttons[i]);
        lv_obj_add_style(s_footer_buttons[i], &s_style_tab_btn_normal, LV_PART_MAIN);
        lv_obj_add_style(s_footer_buttons[i], &s_style_pressed, LV_STATE_PRESSED);
        lv_obj_set_size(s_footer_buttons[i], btn_width, btn_height);
        lv_obj_set_pos(s_footer_buttons[i], offset_left + i * (btn_width + spacing), offset_top);
        lv_obj_clear_flag(s_footer_buttons[i], LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_set_user_data(s_footer_buttons[i], (void*)(intptr_t)i);
        lv_obj_add_event_cb(s_footer_buttons[i], footer_btn_event_cb, LV_EVENT_CLICKED, NULL);

        s_footer_active_strips[i] = lv_obj_create(s_footer_buttons[i]);
        lv_obj_remove_style_all(s_footer_active_strips[i]);
        lv_obj_set_size(s_footer_active_strips[i], btn_width - 18, 3);
        lv_obj_set_pos(s_footer_active_strips[i], 9, 4);
        lv_obj_set_style_bg_color(s_footer_active_strips[i], COL_ACCENT, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_footer_active_strips[i], LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(s_footer_active_strips[i], 2, LV_PART_MAIN);
        lv_obj_add_flag(s_footer_active_strips[i], LV_OBJ_FLAG_HIDDEN);

        lv_obj_t *lbl = lv_label_create(s_footer_buttons[i]);
        lv_label_set_text(lbl, s_tab_names[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, LV_PART_MAIN);
        lv_obj_set_style_text_color(lbl, COL_TEXT_MUTED, LV_PART_MAIN);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 4);
```

This makes the button touch target `106x47`, leaves 4 px top and bottom inside the 55 px footer, and reserves a visible top strip for the active tab.

- [ ] **Step 3: Set the initial active strip**

Replace the current initial active line:

```c
    // Set first tab as active
    lv_obj_add_style(s_footer_buttons[0], &s_style_tab_btn_active, LV_PART_MAIN);
```

with:

```c
    // Set first tab as active
    lv_obj_add_style(s_footer_buttons[0], &s_style_tab_btn_active, LV_PART_MAIN);
    lv_obj_remove_flag(s_footer_active_strips[0], LV_OBJ_FLAG_HIDDEN);
```

- [ ] **Step 4: Build footer polish**

Run:

```powershell
cd D:\AI\CDJ-XXX\repo\firmware\main-deck-p4
$activate = & "C:\Espressif\python_env\idf5.5_py3.13_env\Scripts\python.exe" "C:\Espressif\.espressif\v5.5.4\esp-idf\tools\activate.py" --export | Select-Object -First 1; . $activate; idf.py build
```

Expected result: build exits `0`, output includes `Project build complete`, and no warnings mention `components/ui/ui.c`.

- [ ] **Step 5: Commit footer polish**

Run:

```powershell
cd D:\AI\CDJ-XXX\repo
git add firmware/main-deck-p4/components/ui/ui.c
git commit -m "ui: polish footer tabs"
```

Expected result: one commit containing only the footer/tab UI changes.

---

### Task 4: Documentation And Final Verification

**Files:**
- Modify: `docs/development-plan.md`

- [ ] **Step 1: Update UI polish checklist**

In `docs/development-plan.md`, under the existing UI polish checklist, after the Key Shift item, add:

```markdown
    - [x] Footer tabs: retained the seven-tab navigation model while refining
      active, normal, pressed and future disabled states into a clearer
      performance-bar footer.
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
git commit -m "docs: mark footer tabs polish complete"
```

Expected result: one documentation-only commit.

---

## Manual Visual Check

After the firmware build, inspect on simulator or hardware when practical:

- All seven tab labels fit inside their fixed `106x47` touch targets.
- The active tab has accent fill, accent border, bright text and visible top strip.
- Normal tabs are muted and lower contrast than the active tab.
- Press feedback still dims tabs on touch.
- Footer labels do not overlap or clip at 800x480.

## Self-Review

- Spec coverage: Tasks 1-3 implement the approved option A footer style, active strip, touch target refinement, pressed preservation and future disabled style. Task 4 records the completed polish in the project plan.
- Type consistency: The plan uses existing LVGL APIs, existing `COL_*` tokens, existing `s_style_pressed`, existing `s_footer_buttons[7]`, existing `s_tab_names[7]`, and a new `s_footer_active_strips[7]`.
- Verification coverage: The plan includes `git diff --check`, firmware build, compiler warning review, and manual 800x480 visual checks.
