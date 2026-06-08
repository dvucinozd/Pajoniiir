# UI Polish: Empty Loading Error States

Date: 2026-05-26
Status: Approved design, pending implementation plan

## Scope

This polish pass targets short user-facing state messages in the ESP32-P4 main
deck UI. The approved direction is option A: a status vocabulary pass that
keeps the current layout and behavior, while making empty, loading, busy,
success and error states consistent.

This pass does not redesign screens. It normalizes state text and state colors
for existing labels and status surfaces.

Out of scope:

- Adding new empty-state cards or panels.
- Changing table layout, source selector layout or Settings layout.
- Changing CDJ Link protocol, cache retry logic or media load control flow.
- Introducing a cross-component status enum.
- Changing audio playback behavior.
- Changing footer navigation polish.

## Goals

- Make status language consistent across Library, header status and Settings
  link state.
- Keep all messages short enough for the existing 800x480 layout.
- Make action/loading, success, warning and error states visually distinct.
- Keep remote cache/load errors understandable without exposing too much
  implementation detail.
- Avoid creating new states that imply behavior the firmware does not yet have.

## Status Vocabulary

Use uppercase, compact phrases for operational states.

Recommended label text:

- Idle Library hint: `SELECT TRACK\nPRESS LOAD`
- Duplicate load tap / load in flight: `LOAD BUSY`
- Local load start: `LOADING`
- Remote cache start: `CACHE START`
- Audio engine preload: `LOADING NN%`
- Successful remote join: `JOINED`
- Join scan/no peer yet: `JOIN SCANNING`
- Join refresh failure: `JOIN FAILED`
- Remote peer unavailable while caching: `JOIN OFFLINE`
- Host USB currently busy: `HOST BUSY`
- Manifest fetch failure: `MANIFEST ERR`
- DAT fetch failure: `DAT ERR`
- Audio fetch failure: `AUDIO ERR`
- Generic load failure: `LOAD ERR`
- Successful cache completion: `CACHE READY`
- Successful UI load: `TRACK LOADED`
- Local source label with no tracks: `LOCAL USB  0 TRACKS`
- Joined source label with no tracks: `JOINED  0 TRACKS`

Existing screen-specific stable labels may remain unchanged when they already
read well, including `NO ACTIVE LOOP` and `NO TRANSPOSITION`.

## Visual Language

Use existing theme tokens:

- Idle/info: `COL_TEXT_DIM`
- Loading/action: `COL_ACCENT`
- Success: `COL_GREEN`
- Warning/busy: `COL_AMBER`
- Error/blocker: red via existing red token or `lv_color_hex(0xFF1744)`

Status updates should apply both text and color where the label already has a
dedicated status role. This pass should not recolor arbitrary labels that are
not status labels.

## Component Boundaries

Implementation should stay focused on:

- `firmware/main-deck-p4/components/ui/ui.c`
- `firmware/main-deck-p4/components/remote_cache/remote_cache.c`

Recommended approach:

- Add small local helper functions in `ui.c` to set status text and color for
  `s_label_status_indicator`.
- Add a helper in `ui.c` to map remote cache status strings to display color.
- Update Library hint strings through `ui_library_set_load_busy()`.
- Update Join state strings in `library_source_joined_event_cb()` and
  `ui_update_link_status_label()`.
- Update `remote_cache.c` short status strings where the wording is currently
  inconsistent, such as `NO PEER` and `READY`.

No new public API is required for this pass.

## Data Flow

The existing load flow remains intact:

1. User taps `LOAD TRACK`.
2. UI marks the load button busy and shows a short hint.
3. Local source loads through existing `media_catalog_load()`.
4. Remote source updates cache status through `remote_cache_status()`.
5. UI maps the final or current status text to the correct display color.
6. Successful load shows `TRACK LOADED`; failures show the most specific short
   error available.

The existing Settings link status flow remains intact:

1. `wifi_link_get_status()` reports local mode.
2. JOIN mode checks `cdj_link_client_get_peer()`.
3. UI displays `JOINED ...` when a peer exists and `JOIN SCANNING` otherwise.

## Error And Empty States

This pass treats status messages as the empty/loading/error system for V1. It
does not introduce new visual containers.

Error messages should be short but actionable:

- `JOIN OFFLINE`: no host peer is available for remote cache.
- `HOST BUSY`: host is alive but USB is busy; retry behavior remains existing
  firmware behavior.
- `MANIFEST ERR`, `DAT ERR`, `AUDIO ERR`: remote track assets failed at the
  named stage.
- `LOAD ERR`: fallback when no more specific status is available.

## Testing

Minimum verification:

- `git diff --check` from the repository root.
- `idf.py build` from `firmware/main-deck-p4` using the project ESP-IDF
  environment.
- No new compiler warnings from `components/ui/ui.c` or
  `components/remote_cache/remote_cache.c`.
- Manual or code inspection:
  - Library hint uses `SELECT TRACK\nPRESS LOAD`.
  - Remote load start uses `CACHE START`.
  - `NO PEER` is no longer user-facing; use `JOIN OFFLINE`.
  - `READY` cache completion is user-facing as `CACHE READY`.
  - `JOIN scanning` casing is replaced by `JOIN SCANNING`.
  - Error states use red, busy states use amber, loading uses accent and
    success uses green where the existing status label supports color.

## Acceptance Criteria

- Existing load, cache and join behavior is unchanged.
- All changed user-facing status strings use the approved vocabulary.
- Header status color matches the category of the active state.
- Library hint and load button states remain readable within the existing
  right-side Library controls.
- Build passes with no new `ui.c` or `remote_cache.c` warnings.
