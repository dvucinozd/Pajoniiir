# UI Polish: Beat Jump

Date: 2026-05-26
Status: Approved design, pending implementation plan

## Scope

This polish pass targets only the `BEAT JUMP` tab in the ESP32-P4 main deck UI.
The approved visual direction is option A from the browser mockups: two
direction lanes, with `BACKWARD` on the top row and `FORWARD` on the bottom row.

This pass keeps all existing Beat Jump behavior. It reorganizes presentation and
state readability only.

Out of scope:

- Changing beatgrid parsing or PQTZ handling.
- Changing `jump_btn_event_cb()` behavior.
- Changing `audio_engine`, `deck_core`, media loading or timing logic.
- Adding new jump sizes.
- Adding slip, quantize, roll or loop-roll behavior.
- Polishing Key Shift, Settings or footer navigation.

## Goals

- Keep the current two-row muscle memory.
- Make jump direction readable before touching the screen.
- Keep each jump button large enough for hardware use.
- Bring button styling in line with the polished Hot Cues, Loop and Settings
  surfaces.
- Remove the explanatory sentence-style header from the screen.
- Preserve existing callback wiring and user data values.

## Layout Design

The screen remains a dedicated `BEAT JUMP` tab.

The layout has two horizontal lanes:

- Top lane: `BACKWARD`
  - Buttons: `-1`, `-4`, `-8`, `-16`
- Bottom lane: `FORWARD`
  - Buttons: `+1`, `+4`, `+8`, `+16`

Each lane should have a small uppercase label. The lane labels replace the
current centered explanatory text `Jump forward or backward on beatgrid`.

Button labels should be compact and consistent:

- `-1 BEAT`
- `-4 BEATS`
- `-8 BEATS`
- `-16 BEATS`
- `+1 BEAT`
- `+4 BEATS`
- `+8 BEATS`
- `+16 BEATS`

The exact text may omit the word `BEAT` only if implementation finds that it
does not fit cleanly at 800x480, but the jump value and direction must remain
unambiguous.

## Visual Language

Use existing theme tokens and shared styles:

- `COL_PANEL_DK`, `COL_SURFACE`, `COL_BORDER_LT`
- `COL_TEXT`, `COL_TEXT_MUTED`
- `COL_GREEN`, `COL_RED`, `COL_AMBER`
- `s_style_panel_frame`, `s_style_pressed`

Expected state color language:

- Backward lane: restrained red/destructive border and subtle dark red fill.
- Forward lane: restrained green/positive border and subtle dark green fill.
- Lane labels: amber or muted color for backward, green or muted color for
  forward. The labels must be readable but not more prominent than the buttons.

Avoid the old inline orange-red look for backward buttons. The screen should
feel like a focused performance-control surface, not a warning panel.

## Component Boundaries

Implementation should stay inside:

- `firmware/main-deck-p4/components/ui/ui.c`

Recommended approach:

- Add a small local helper for creating one Beat Jump button if it reduces
  duplication.
- Keep `jump_btn_event_cb()` unchanged.
- Keep existing user data values:
  - backward values are negative.
  - forward values are positive.
- Do not modify `deck_core`, `audio_engine`, `library`, `media_catalog`,
  `remote_cache`, USB, SD or CDJ Link components.

## Data Flow

The Beat Jump screen continues to create eight LVGL buttons. Each button keeps
the existing callback:

```c
lv_obj_add_event_cb(btn, jump_btn_event_cb, LV_EVENT_CLICKED, NULL);
```

Each button keeps the existing user data pattern:

- `-jump_vals[i]` for backward buttons.
- `jump_vals[i]` for forward buttons.

The polish layer should not introduce new persistent state.

## Error And Empty States

Beat Jump has no screen-specific empty or error state in this pass. If the deck
cannot jump because no track or beatgrid is available, existing runtime behavior
continues to own that decision.

## Testing

Minimum verification:

- `idf.py build` from `firmware/main-deck-p4` using the project ESP-IDF
  environment.
- No new compiler warnings from `components/ui/ui.c`.
- `git diff --check` from the repository root.
- Visual/manual inspection on simulator or hardware when practical:
  - Two lanes are visually distinct.
  - Button text fits at 800x480.
  - Touch targets remain large.
  - Backward and forward buttons still call the existing event path.

## Acceptance Criteria

- Beat Jump is visually consistent with the polished screens.
- Direction is clear before pressing any button.
- Existing jump sizes and callback behavior are unchanged.
- No text overlaps or clipped labels at 800x480.
- Build passes with no new `ui.c` warnings.
