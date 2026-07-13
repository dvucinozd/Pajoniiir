# UI Polish: Key Shift

Document status (2026-07-13): superseded UI concept. The dedicated Key Shift
screen was removed. Master Tempo/key lock is exposed by `MT` on Overview.

Date: 2026-05-26
Original status superseded: Key Shift touch UI was removed; Overview now owns
the Master Tempo toggle.

## Scope

This polish pass targets only the `KEY SHIFT` tab in the ESP32-P4 main deck UI.
The approved visual direction is option A from the browser mockups: two large
control panels, with `MASTER TEMPO` on the left and `KEY TRANSPOSE` on the
right.

This pass keeps all existing Key Shift behavior. It reorganizes layout,
typography and state readability only.

Out of scope:

- Changing `keylock_toggle_event_cb()` behavior.
- Adding real transpose controls.
- Adding semitone state, key analysis or audio pitch/key processing.
- Changing `deck_core`, `audio_engine`, media loading or playback behavior.
- Polishing footer navigation or global empty/loading/error states.

## Goals

- Make the screen visually consistent with Settings, Beat Jump, Loop and Hot
  Cues.
- Keep the existing left/right structure so the current interaction remains
  obvious.
- Make `MASTER TEMPO` and `KEY LOCK` easier to read as one concept.
- Make `KEY TRANSPOSE` read like a clear current-state display instead of an
  unfinished control.
- Preserve the existing switch callback and runtime behavior.

## Layout Design

The screen remains a dedicated `KEY SHIFT` tab.

The layout has two framed panels:

- Left panel: `MASTER TEMPO`
  - Small uppercase section label: `MASTER TEMPO`
  - Large primary label: `KEY LOCK`
  - Existing LVGL switch remains the touch control.
  - Secondary helper text: `PRESERVES KEY`
- Right panel: `KEY TRANSPOSE`
  - Small uppercase section label: `KEY TRANSPOSE`
  - Large primary readout: `ORIGINAL KEY`
  - Secondary helper text: `NO TRANSPOSITION`

The left panel should not imply new firmware state that does not exist yet.
`PRESERVES KEY` is descriptive helper text for the existing switch concept, not
a live keylock status indicator.

## Visual Language

Use existing theme tokens and shared styles:

- `COL_PANEL_DK`, `COL_SURFACE`, `COL_BORDER_LT`
- `COL_TEXT`, `COL_TEXT_MUTED`, `COL_TEXT_DIM`
- `COL_GREEN`, `COL_ACCENT`
- `s_style_panel_frame`, `s_style_pressed`

Expected state language:

- Green emphasizes `KEY LOCK` / key-preserving behavior.
- White primary text emphasizes `ORIGINAL KEY`.
- Dim secondary text explains the current no-transposition state.
- Avoid oversized decorative cards and keep card radius consistent with the
  polished screens.

## Component Boundaries

Implementation should stay inside:

- `firmware/main-deck-p4/components/ui/ui.c`

Recommended approach:

- Reuse `s_style_panel_frame`.
- Reuse `ui_label_set_small_caps()` for section labels.
- Add only small local helpers if needed for readability.
- Keep `keylock_toggle_event_cb()` unchanged.
- Keep the existing LVGL switch and event callback wiring:

```c
lv_obj_add_event_cb(sw_keylock, keylock_toggle_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
```

## Data Flow

The Key Shift screen continues to create one LVGL switch for keylock/master
tempo. The switch continues calling `keylock_toggle_event_cb()`.

The transpose panel remains display-only for this polish pass. It should not
introduce new state, new callbacks or new settings.

## Error And Empty States

Key Shift has no screen-specific error or empty state in this pass.

## Testing

Minimum verification:

- `idf.py build` from `firmware/main-deck-p4` using the project ESP-IDF
  environment.
- No new compiler warnings from `components/ui/ui.c`.
- `git diff --check` from the repository root.
- Visual/manual inspection on simulator or hardware when practical:
  - `MASTER TEMPO`, `KEY LOCK`, `PRESERVES KEY`, `KEY TRANSPOSE`,
    `ORIGINAL KEY` and `NO TRANSPOSITION` fit at 800x480.
  - The keylock switch remains reachable.
  - The existing switch callback is still wired.

## Acceptance Criteria

- Key Shift is visually consistent with the polished screens.
- Existing keylock switch behavior is unchanged.
- The transpose area clearly reads as current-state display only.
- No text overlaps or clipped labels at 800x480.
- Build passes with no new `ui.c` warnings.
