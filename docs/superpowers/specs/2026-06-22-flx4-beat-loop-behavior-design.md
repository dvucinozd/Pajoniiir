# FLX4 Beat Loop Behavior Design

## Goal

Implement P4-owned DDJ-FLX4 Beat Loop pad behavior for the already-mapped
Beat Loop pad action events.

## Scope

- Beat Loop pads 1-8 are handled in `deck_core` when the pad action mode is
  `CTRL_PAD_MODE_BEAT_LOOP`.
- The S3 MIDI mapping and `0xA5` control-link protocol remain unchanged.
- Hardware smoke remains pending after firmware build.
- Shifted Beat Loop pad behavior and Beat Loop pad LEDs remain out of scope
  for this slice.

## Pad Mapping

| Pad | Loop length |
| ---: | ---: |
| 1 | 1/32 beat |
| 2 | 1/16 beat |
| 3 | 1/8 beat |
| 4 | 1/4 beat |
| 5 | 1/2 beat |
| 6 | 1 beat |
| 7 | 2 beats |
| 8 | 4 beats |

## Runtime Behavior

On a pressed Beat Loop pad:

1. `deck_core` reads the addressed deck's current position.
2. It calculates a loop duration from local ANLZ beatgrid spacing when
   available.
3. It falls back to BPM, using 120 BPM when no BPM is available.
4. It calls `audio_engine_deck_set_loop(deck, start_ms, end_ms)`.
5. It preserves the deck's current play/pause state.

Release events are no-ops. The behavior is deck-local.

## Beat Duration Calculation

The implementation will reuse the Beat Jump metadata hooks:

- `ui_get_deck_anlz_metadata(deck)` for ANLZ beatgrid.
- `ui_library_deck_bpm(deck, 120)` for BPM fallback.

Beatgrid duration is estimated from adjacent beat timestamps around the current
position. BPM fallback uses `60000 / bpm` milliseconds per beat.

## Verification

Host tests must cover:

- Beat Loop pads set loops only on the addressed deck.
- Pad mapping produces expected loop lengths.
- Release events do not change loop state.
- BPM fallback works without beatgrid metadata.
- Existing loop, Hot Cue, and Beat Jump tests remain green.

Firmware verification:

- `.\tests\run_p4_host_tests.ps1`
- `idf.py build` in `firmware\main-deck-p4`

## Hardware Smoke Handoff

After flashing later, test:

1. Load a beatgrid-backed track.
2. Select Beat Loop mode.
3. Press pads 4, 6, 7, and 8 on Deck 1.
4. Confirm Loop In/Out LEDs reflect active loop state.
5. Repeat at least pads 6 and 8 on Deck 2.
6. Confirm audio continues normally while loop is active.
