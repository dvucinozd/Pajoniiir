# UI Polish: Hot Cues + Loop

Document status (2026-07-13): partially superseded. Hot Cues remains a touch
tab; the dedicated Loop touch screen was removed and loop stays controller-led.

Date: 2026-05-26
Original status superseded: Hot Cues is implemented; the Loop touch tab was
removed.

## Scope

This polish pass targets the two performance-control screens that sit after
Overview and Library:

- `HOT CUES`
- `LOOP`

The approved visual direction is option C from the browser mockups: keep the two
screens separate and focused, but make each calmer, more readable and more
stateful. This pass does not merge the screens and does not change playback,
cue, loop or audio behavior.

Out of scope:

- New hot-cue editing workflows.
- Renaming, deleting, moving or writing cues back to USB.
- A combined performance page.
- Beat Jump, Key Shift or Settings polish.
- Loop engine behavior changes.

## Goals

- Make cue, hot-loop and empty pad states readable at a glance.
- Keep all existing touch targets large and easy to hit.
- Make the Loop screen show active loop state more clearly.
- Reduce visual noise by using the same restrained CDJ/XDJ surface language
  introduced in the Overview and Library polish.
- Preserve existing callbacks, data flow and timing-sensitive audio behavior.

## Hot Cues Design

The screen stays a 2x4 grid of pads labelled `CUE A` through `CUE H`.

Pad states:

- **Cue set:** green border/fill emphasis, label `CUE <letter>`, time shown in
  the bottom-right corner.
- **Hot loop set:** amber border/fill emphasis, label `LOOP <letter>`, start
  time shown in the bottom-right corner.
- **Empty:** dark/dim panel, label `CUE <letter>`, bottom-right text `EMPTY`.

The current full rainbow cue palette should be retired for this screen. It makes
state harder to read than the cue-vs-loop distinction. Per-cue colors may still
be kept internally for future waveform marker work, but the pad state language
for this pass is semantic: green cue, amber loop, dim empty.

The current `ui_update_hot_cues()` behavior remains the source of truth for
loaded Rekordbox metadata:

- Real ANLZ cues populate pad labels and times.
- Missing cue slots become empty pads.
- Simulator/fallback values may still populate demo cues when no ANLZ metadata
  is present.

## Loop Design

The Loop screen stays separate from Hot Cues and keeps the current 2x3 grid:

- `1 BEAT`
- `2 BEATS`
- `4 BEATS`
- `8 BEATS`
- `16 BEATS`
- `32 BEATS`

The grid should use restrained secondary button styling by default. The active
loop value should be clearly highlighted with the primary blue accent and a
short status label near the top of the screen, for example:

`ACTIVE: 8 BEATS`

When no loop is active, the status should be stable and low-contrast:

`NO ACTIVE LOOP`

The existing red `EXIT LOOP` action stays separate from the loop-size grid. It
should use the shared red/destructive visual language rather than the older
green neon button style with an inline red override.

## Components And Boundaries

Implementation should stay inside:

- `firmware/main-deck-p4/components/ui/ui.c`
- `firmware/main-deck-p4/components/ui/ui_theme.h` only if one or two shared
  color/style tokens are genuinely needed.

Recommended approach:

- Reuse existing shared styles added during Overview/Library polish:
  `s_style_panel_frame`, `s_style_btn_primary`, `s_style_btn_amber`,
  `s_style_btn_secondary`, `s_style_btn_disabled`, and `s_style_pressed`.
- Add only small local helpers for repeated pad styling if that reduces
  duplication inside `ui.c`.
- Keep existing event callbacks:
  `hot_cue_event_cb`, `loop_btn_event_cb`, and `exit_loop_event_cb`.
- Do not change `deck_core`, `audio_engine`, `media_catalog`, `library`,
  `remote_cache`, USB, SD or CDJ Link components.

## Data Flow

Hot Cues:

- `ui_update_hot_cues()` reads the active `anlz_metadata_t` through
  `ui_current_anlz()`.
- It continues updating `s_hot_cue_positions`, `s_hot_cue_ends` and
  `s_hot_cue_types`.
- It continues updating overview waveform cue markers.

Loop:

- Loop state remains driven by the existing screen callbacks and the existing
  local loop state variables:
  `s_loop_active`, `s_loop_start_ms`, and `s_loop_end_ms`.
- The polish layer may add a label that reflects active/inactive loop status,
  but it must not introduce a new loop state machine.

## Error And Empty States

- If a track has ANLZ metadata but no cue in a slot, that pad shows `EMPTY`.
- If there is no loaded track or no ANLZ metadata, simulator/fallback behavior
  can remain as it is today unless implementation finds a clear, low-risk path
  to show all pads as empty on firmware.
- If no loop is active, the Loop screen shows `NO ACTIVE LOOP`.
- Pressing `EXIT LOOP` when no loop is active should remain harmless.

## Testing

Minimum verification:

- `idf.py build` from `firmware/main-deck-p4` using the ESP-IDF environment that
  supports this project.
- No new compiler warnings from `components/ui/ui.c`.
- Hardware smoke on the P4 when practical:
  - Hot Cues screen shows cue/loop/empty states after a track load.
  - Hot Cue pads still trigger the existing cue path.
  - Loop buttons still activate the existing loop path.
  - Exit Loop still clears the existing loop state.
  - No text overlaps at 800x480.

## Acceptance Criteria

- Hot Cues and Loop feel visually related to the polished Overview and Library
  screens.
- Hot Cue semantic states are clear: cue, loop, empty.
- Loop active/inactive state is visible without reading logs.
- Existing touch behavior remains unchanged.
- Existing playback, cue, waveform marker and loop behavior remains unchanged.
- Build passes with no new `ui.c` warnings.
