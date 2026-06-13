# P4 UI Architecture Refactor Design

Date: 2026-06-12

## Goal

Refactor the P4 UI so `ui.c` stops owning every concern at once. The refactor should improve stability and make waveform fluidity work safer by separating display backend code, update orchestration, overview rendering, screen modules, and async hardware-facing work.

This is an architectural cleanup, not a visual redesign. The UI should keep the current behavior and layout unless a change is explicitly needed to preserve stability or reduce jank.

## Current Problem

`firmware/main-deck-p4/components/ui/ui.c` is about 5100 lines and currently mixes:

- LVGL and ESP32-P4 display backend setup.
- PPA partial flush and waveform overlay blits.
- Screen construction for overview, library, hot cues, beat loop, beat jump, key shift, and settings.
- Event handlers for controls and library actions.
- Async media/track loading.
- Per-frame UI state polling and LVGL widget updates.
- Performance diagnostics for UI update, LVGL handler, flush, and waveform rendering.

That makes small fixes risky because unrelated responsibilities live in the same file and share globals. The recent waveform work showed this directly: display flush, overview waveform redraw, LVGL invalidation, USB/media state, and diagnostics all needed to be reasoned about together.

## Architecture

The target architecture keeps `ui.c` as a thin orchestrator and moves domain ownership into focused modules.

### `ui_lvgl_backend`

Owns firmware-only LVGL backend setup:

- `lv_init`, display creation, tick timer, handler task, touch registration.
- ESP32-P4 PPA partial flush callback.
- DSI framebuffer handles and cache alignment.
- LVGL backend performance hooks.
- `ui_lvgl_lock()` and `ui_lvgl_unlock()`.

Consumers should not know how the PPA flush is configured or how LVGL dirty rectangles map to the physical 480x800 panel.

### `ui_state`

Owns shared UI state and caches that are not screen-specific:

- Loaded deck title/artist metadata.
- Active deck/performance target bridge.
- Status indicator override state.
- Timing buckets for elapsed/remaining labels.
- Helper functions for cache invalidation and changed-only LVGL updates.

This module should avoid screen construction. It may expose small state structs and cache helpers used by screen modules.

### `ui_overview`

Owns the overview screen:

- Overview screen creation.
- Deck panels and their LVGL object references.
- Main and mini waveform canvases.
- Playhead, time/BPM/pitch labels, cue markers.
- Beat indicator, phase meter, mixer overview.

The module may call renderer/model helpers, but scheduling decisions for expensive redraws should be delegated.

### `ui_overview_scheduler`

Owns expensive overview work budgeting:

- Main waveform redraw cadence and per-tick budget.
- Round-robin deck ordering for waveform redraws.
- Decisions about when to render, defer, or skip a heavy update.

The implementation started with one main overview waveform redraw per UI update tick. After the Deck 2 jitter follow-up, the scheduler uses an adaptive budget: one redraw normally, two redraws when both decks are playing. Deck 2 direct overlay remains disabled because that lower framebuffer path jittered on hardware; Deck 2 uses normal LVGL invalidation/flush.

### Screen Modules

Screen construction and event handlers should move into modules by ownership:

- `ui_library`: library table, selection, load buttons, source labels.
- `ui_track_load`: async media load worker/result queue and loaded-track application.
- `ui_controls`: play/cue/hot cue/loop/jump/keylock controls.
- `ui_settings`: settings screen and system status labels.

These modules should expose init/create/update functions rather than sharing direct implementation details through `ui.c`.

### `ui.c`

After the refactor, `ui.c` should own:

- `ui_init()`.
- `ui_update()`.
- Screen registry/top-level tab switching.
- Construction order between modules.
- A per-frame context passed into update modules.

The target after phases 2-6 is for `ui.c` to be under 1000 lines, with a stretch target of 500-800 lines if the remaining orchestration stays readable.

## Data Flow

`ui_update()` becomes a frame orchestrator.

First it gathers snapshots:

- Deck state for D1 and D2.
- Active deck and performance target.
- Track durations and metadata pointers.
- Audio loading and mixer state.
- Beat indicator state for the active deck.
- Tab visibility and slow-update bucket.
- Current tick/time.

Then it builds a `ui_frame_context_t` with those values and calls update modules:

- `ui_status_update(&ctx)`
- `ui_controls_update(&ctx)`
- `ui_overview_update(&ctx)` when the overview tab is visible.
- `ui_library_update(&ctx)` when the library tab is active or refresh is pending.
- `ui_settings_update(&ctx)` when the settings tab is active.

Modules should distinguish three concepts:

- State changed.
- LVGL widget needs a cheap update.
- Expensive rendering work is allowed this tick.

This makes the waveform work tunable through scheduler policy instead of scattered conditionals inside `ui_update()`.

## Error Handling And Stability Rules

Every module that initializes firmware resources should return `esp_err_t`.

LVGL backend rules:

- If panel/PPA initialization fails, `ui_init()` fails.
- Touch absence is non-fatal and logs a warning.
- Flush callback handles null/invalid input defensively and always calls `lv_display_flush_ready()`.
- PPA errors are rate-limited or aggregated so diagnostics do not create UI jank.

Scheduler rules:

- Heavy overview work must be budgeted.
- Main waveform redraw is budgeted centrally; the current policy allows both decks only when both are playing.
- Deferred redraw must not block cheap label/playhead/mini updates.

Async load rules:

- Media load worker/result queue lives outside screen code.
- UI applies completed results on the LVGL/UI side.
- SD/media loader failures produce explicit UI states and do not leave partial track metadata active.

## Implementation Phases

### Phase 1: Baseline Cleanup

Keep the current partial-flush firmware behavior as the baseline. Reduce temporary performance logging so measurements do not materially affect runtime behavior. Keep the P4 rev selector fix and current waveform overlay optimizations.

Checkpoint:

- Host helper tests where available.
- `idf.py build`.
- Flash and 45-second monitor if logging or update cadence changes.

### Phase 2: Extract `ui_lvgl_backend`

Move LVGL/P4 backend code out of `ui.c`:

- Display init and buffer setup.
- Partial PPA flush.
- Touch read callback.
- LVGL tick/handler task.
- LVGL backend perf counters.

`ui.c` should call a backend init function and keep no PPA flush internals.

Checkpoint:

- `idf.py build`.
- Flash to COM15.
- 45-second monitor focused on boot, LVGL handler duration, flush timing, and panics/asserts.

### Phase 3: Extract `ui_overview_scheduler`

Move overview redraw policy into a small testable module:

- Per-update main waveform budget.
- Round-robin deck ordering.
- Decision function for main waveform redraw allowance.

Add host unit tests for scheduler behavior.

Checkpoint:

- Host scheduler tests.
- `idf.py build`.
- Flash/monitor if update loop behavior changed.

### Phase 4: Extract `ui_overview`

Move overview screen creation and update logic:

- Deck panel struct and widgets.
- Main/mini waveform update.
- Beat indicator, phase meter, cue markers, mixer overview.

The extracted module uses `ui_overview_scheduler` for heavy redraw decisions.

Checkpoint:

- Existing host tests.
- `idf.py build`.
- Flash/monitor because this phase touches visible runtime behavior.

### Phase 5: Extract Library And Track Load

Move library table and async track load ownership:

- Library rows, sorting, source label, selection.
- Async track load worker and result polling.
- Loaded track application and error paths.

Checkpoint:

- `idf.py build`.
- Flash/monitor when async load path changes.

### Phase 6: Introduce `ui_frame_context_t` - Complete

Make `ui_update()` gather state once and pass a context into modules. This phase should remove repeated snapshot reads and make update ordering explicit.

Closed on 2026-06-12:

- `ui_frame_context_t` now carries deck snapshots, active target, timing, beat, mixer, loading, tab, and slow-update state.
- `ui_update()` now builds one context and calls `ui_library_update`, `ui_status_update`, `ui_overview_update`, and `ui_settings_update` in explicit order.
- Status/header/time/BPM/pitch/legacy LED updates moved from `ui.c` into `ui_status`.
- Settings system status polling moved from `ui.c` into `ui_settings`.
- `ui.c` no longer owns the removed status/settings cache globals.

Checkpoint:

- Host tests for pure helpers passed.
- `idf.py build` passed for `firmware/main-deck-p4`.
- Flash/monitor remains part of the Phase 7 stabilization checkpoint.

### Phase 7: Stabilization Pass - Complete

Review module boundaries and remove temporary diagnostics or gate them behind explicit diagnostic macros. Keep only useful aggregated performance logs.

Closed on 2026-06-13:

- Settings screen construction and callbacks were extracted from `ui.c` into `ui_settings`.
- `ui.c` is now 887 lines and remains a top-level orchestrator for init, screen registry, tab switching, and frame context construction.
- `git diff --check` passed.
- All `tests/ui_*` host tests passed.
- `idf.py build` passed for `firmware/main-deck-p4`.
- Flash to COM15 passed with app version `5f9b425`.
- 45-second COM15 monitor captured 26 lines with `bad_lines=0`.

### Post-Phase 7: Deck 2 Overview Waveform Stabilization - Complete

Closed on 2026-06-13:

- Deck 2 lower Overview waveform jitter was traced to the direct overlay path on the lower framebuffer region.
- `ui_overview_scheduler` now exposes the redraw budget and direct-overlay policy separately.
- The redraw budget is adaptive: one deck normally, two decks when both decks are playing.
- Direct PPA overlay remains enabled for Deck 1 and disabled for Deck 2; Deck 2 falls back to LVGL invalidate/flush.
- Hardware visual confirmation showed Deck 2 no longer jittering.

## Testing Strategy

Use host tests for pure logic:

- Overview redraw scheduler.
- Existing motion, waveform model, beat indicator, mixer view, and position interpolation helpers.

Use firmware verification for hardware-bound behavior:

- LVGL backend extraction.
- PPA partial flush.
- Touch registration.
- Track loading and SD/media interactions.

Use monitor evidence after risky phases:

- No panic/assert.
- LVGL flush total remains in low milliseconds or below.
- UI update and LVGL handler do not return to repeated 100-300 ms spikes in steady state.

## Non-Goals

- No visual redesign.
- No new controls or deck features.
- No rewrite of the waveform renderer unless later measurements show it is necessary.
- No broad change to audio engine, deck core, SD driver, or media parser behavior.

## Success Criteria

- `ui.c` is under 1000 lines after phases 2-7 and no longer contains LVGL backend internals.
- Display backend changes can be reviewed and tested independently.
- Overview redraw policy is testable without hardware.
- `ui_update()` reads like an orchestrator instead of a full application implementation.
- Firmware still builds and runs on ESP32-P4.
- The current partial-flush waveform-fluidity improvement is preserved.
