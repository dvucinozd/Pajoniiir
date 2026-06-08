# UI Polish: Overview + Library

Date: 2026-05-26
Status: Approved concept, pending implementation plan

## Scope

This polish pass targets the two most-used screens: `OVERVIEW` and `LIBRARY`.
The visual direction is hybrid: a restrained CDJ/XDJ-style working surface with
neon emphasis only where it improves deck feedback, especially waveform, beat,
cue and cache/load states.

Out of scope for this pass: full redesign of Hot Cues, Loop, Beat Jump, Key
Shift and Settings. Shared style helpers may touch those screens only when
needed to keep existing controls consistent.

## Goals

- Improve first-glance readability for loaded track, playback status, BPM,
  elapsed/remaining time and source state.
- Make waveform, playhead, hot-cue markers and beat indicator feel intentional
  rather than decorative.
- Make the library faster to scan and harder to mis-tap.
- Make remote-cache states visible: `LOCAL USB`, `JOINED`, `HOST BUSY`,
  `SD CACHE REQUIRED`, `CACHING`, `LOADING` and load errors.
- Preserve current behavior and timing-sensitive audio/UI paths.

## Overview Design

The screen keeps the existing structure: header, overview waveform, high-res
zoom waveform, transport controls and beat indicator.

Polish changes should focus on hierarchy:

- Header: title and artist stay dominant; BPM, pitch and time counters become
  more compact and aligned. Status text should avoid competing with the title.
- Waveforms: preserve current I8 canvas approach and playhead movement. Tune
  surrounding spacing, borders and color intensity so waveform is the visual
  center without turning the whole screen into a glow effect.
- Transport: PLAY and CUE remain large touch targets. Their visual state should
  make playing/paused/cue-ready obvious.
- Beat indicator: keep the 4-beat pulse row, but align it visually with the
  waveform section and ensure it reads as deck timing, not decoration.

## Library Design

The library remains a dense operational list, not a card layout.

Polish changes should focus on scan speed and state clarity:

- Table rows should have consistent height, enough contrast and a persistent
  selected-row style.
- Source/status area should clearly show local vs joined library and any cache
  condition before the user taps load.
- Load button should lock visually while a load/cache operation is in progress.
- Remote cache progress text should be short and stable so it does not resize or
  shift nearby controls.
- Sort controls should remain predictable and not steal visual priority from the
  selected track and load action.

## Components And Boundaries

Implementation should stay inside `firmware/main-deck-p4/components/ui/` unless
a small shared status string already exists in another component.

Recommended structure:

- Extend `ui_theme.h` only for shared chrome/status tokens.
- Add small local UI helper functions in `ui.c` for repeated panel, label and
  button styling.
- Avoid broad refactors of `ui.c`; keep edits near the Overview and Library
  construction/update paths.

## Data Flow

- Existing playback state continues to come from `deck_core_get_state()` and
  `audio_engine_get_state()`.
- Loaded metadata continues through `media_loaded_track_t`, `media_catalog` and
  `ui_current_anlz()`.
- Library source and remote states continue through `media_catalog`,
  `cdj_link_client` and `remote_cache_status()`.
- The polish layer only changes presentation and disable/pressed states; it
  should not introduce new playback or cache state machines.

## Error And Empty States

- No USB or no tracks: Library must show a stable empty state and keep load
  disabled or harmless.
- Joined source offline: show a clear joined/offline state without clearing
  already loaded local playback metadata.
- SD required or cache failure: show the error in the load/status area, not as a
  modal or blocking flow.
- Host busy: show a retry/busy state that matches existing remote-cache retry
  behavior.

## Testing

Minimum verification for implementation:

- `idf.py build` from `firmware/main-deck-p4`.
- PC/simulator build if available for `ui.c` changes.
- Hardware smoke on P4 when practical:
  - Overview still shows title, artist, BPM, time, waveform, cue markers and
    beat indicator after track load.
  - Library selection remains persistent after sorting and row refresh.
  - Local load still works.
  - Remote-source status text does not overlap controls.

## Acceptance Criteria

- Overview and Library look visually related and calmer than before while
  retaining neon deck feedback.
- No text overlap at 800x480 logical landscape resolution.
- Main touch targets remain comfortable for finger use.
- Existing playback, cue, waveform, library and remote-cache behavior remains
  unchanged.
- Build has no new warnings from `ui.c`.
