# UI Polish: Footer Tabs

Document status (2026-07-13): superseded layout concept. Current tabs are
Overview, Library, Hot Cues and Settings.

Date: 2026-05-26
Original status superseded: current footer contains four tabs.

## Scope

This polish pass targets only the global footer/tab navigation in the ESP32-P4
main deck UI. The approved visual direction is option A: a performance-oriented
footer with seven strong touch targets, quieter normal tabs, and a clear active
tab indicator.

This pass keeps all existing navigation behavior. It refines the footer visual
language, active/pressed/disabled states, spacing and touch targets only.

Out of scope:

- Changing the seven tab names or their order.
- Changing `footer_btn_event_cb()` screen switching behavior.
- Adding new tabs or removing existing tabs.
- Changing individual screen layouts.
- Adding icons to the footer.
- Implementing runtime disabled tab logic.
- Polishing global empty/loading/error states.

## Goals

- Make the active tab obvious at a glance on the 800x480 hardware screen.
- Keep all seven tabs easy to hit with touch.
- Reduce visual noise from the normal tab backgrounds and borders.
- Preserve the existing dim-on-press feedback.
- Add a consistent disabled visual style for future use, without disabling any
  tab in this pass.
- Keep `BEAT JUMP` and `KEY SHIFT` readable despite the compact footer width.

## Layout Design

The footer remains a fixed bar:

- Position: bottom of the root UI.
- Size: `800x55`.
- Tab count: 7.
- Tab names: `OVERVIEW`, `LIBRARY`, `HOT CUES`, `LOOP`, `BEAT JUMP`,
  `KEY SHIFT`, `SETTINGS`.

Each tab remains a rectangular LVGL button with a label centered inside it.
The implementation should keep predictable spacing across the entire footer and
avoid dynamic resizing by label length.

The active tab should have a small top accent strip in addition to its active
border and brighter text. This gives the operator an immediate location cue
without making the footer visually heavier than the screen content.

## Visual Language

Use existing theme tokens and shared styles:

- `COL_FOOTER`
- `COL_SURFACE`
- `COL_PANEL_DK`
- `COL_BORDER`
- `COL_BORDER_LT`
- `COL_TEXT`
- `COL_TEXT_MUTED`
- `COL_TEXT_DIM`
- `COL_ACCENT`
- `COL_ACCENT_DK`
- `s_style_pressed`

Expected state language:

- Normal tabs use dark surface fill, muted text and a restrained border.
- Active tab uses accent dark fill, accent border, bright text and a thin top
  accent strip.
- Pressed state keeps the existing color-agnostic dim-on-press style.
- Disabled state uses dim text, dark fill and low-contrast border. It is defined
  for future footer logic, but no current tab is disabled by this polish pass.

## Component Boundaries

Implementation should stay inside:

- `firmware/main-deck-p4/components/ui/ui.c`

Recommended approach:

- Keep `s_footer_buttons[7]` and `s_tab_names[7]`.
- Keep `footer_btn_event_cb()` behavior unchanged.
- Adjust `s_style_footer`, `s_style_tab_btn_normal` and
  `s_style_tab_btn_active`.
- Add one new `s_style_tab_btn_disabled` style.
- Add a small active-strip child per footer button, stored in a new static array
  if needed.
- Update the active strip visibility inside `footer_btn_event_cb()`.
- Keep `lv_obj_add_style(s_footer_buttons[i], &s_style_pressed,
  LV_STATE_PRESSED);`.

## Data Flow

The footer continues to use each button's user data as the target tab index.
When a tab is clicked, `footer_btn_event_cb()` continues to:

- show the selected screen,
- hide all other screens,
- update active tab styling,
- set `s_active_tab`.

The polish may add presentational active-strip visibility updates in that same
loop. It should not change the selected index, screen visibility rules or the
logging behavior.

## Error And Empty States

Footer navigation has no screen-specific error or empty state in this pass.
Disabled styling is presentational infrastructure only; no runtime error state
should disable a tab yet.

## Testing

Minimum verification:

- `git diff --check` from the repository root.
- `idf.py build` from `firmware/main-deck-p4` using the project ESP-IDF
  environment.
- No new compiler warnings from `components/ui/ui.c`.
- Visual/manual inspection on simulator or hardware when practical:
  - All seven tab labels fit at 800x480.
  - Active tab is clearly distinguishable from normal tabs.
  - Press feedback remains visible.
  - Touch targets remain large enough for repeated use.
  - No footer text overlaps or clips.

## Acceptance Criteria

- Footer keeps the existing seven-tab navigation behavior.
- Active, normal, pressed and disabled visual states are defined consistently.
- Active tab is visible at a glance without dominating the UI.
- All tab labels remain readable at 800x480.
- Build passes with no new `ui.c` warnings.
