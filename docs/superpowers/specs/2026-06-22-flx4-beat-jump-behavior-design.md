# FLX4 Beat Jump Behavior Design

Document status (2026-07-13): implemented behavior record. Current acceptance
details live in `../../DDJ_FLX4_MIDI_MAP.md`.

Date: 2026-06-22  
Branch: `codex/phase7-extended-controls-vu`

## Scope

Implement P4-owned Beat Jump playback behavior for the DDJ-FLX4 controls that
are already mapped by S3 and carried through the `0xA5` control link.

This slice covers:

- `Cue/Loop Call Left + Shift` / `Cue/Loop Call Right + Shift`
- Beat Jump performance pads while the controller is in Beat Jump pad mode

This slice does not cover:

- Beat Sync or tempo matching
- Master tempo/key lock
- Beat Jump pad LEDs
- Shifted Beat Jump size inc/dec behavior
- Any Mixxx JavaScript runtime logic

## Behavior

P4 remains authoritative. S3 only forwards semantic events.

Beat Jump performs an audio seek on the addressed deck:

| Control | Beat shift |
| --- | ---: |
| `Cue/Loop Call Left + Shift` | -1 beat |
| `Cue/Loop Call Right + Shift` | +1 beat |
| Beat Jump pad 1 | -32 beats |
| Beat Jump pad 2 | -16 beats |
| Beat Jump pad 3 | -8 beats |
| Beat Jump pad 4 | -4 beats |
| Beat Jump pad 5 | +4 beats |
| Beat Jump pad 6 | +8 beats |
| Beat Jump pad 7 | +16 beats |
| Beat Jump pad 8 | +32 beats |

Only press events act. Release events are consumed without side effects.

## Target Calculation

Use the existing tested target model from
`ui_performance_tabs_calculate_jump_target()`:

- If per-deck ANLZ beatgrid metadata is available, jump by beatgrid index from
  the closest beat to the current deck position.
- Clamp beatgrid jumps to the first/last known beat.
- If beatgrid metadata is unavailable, fall back to BPM timing.
- If BPM is unavailable, use 120 BPM.
- Clamp negative fallback targets to `0 ms`.

The implementation should avoid duplicating this algorithm. If `deck_core`
cannot safely depend on `ui_performance_tabs`, extract the pure calculation
into a small shared component or a deck-core-local helper with equivalent host
tests.

## Metadata Source

`deck_core` needs per-deck beat metadata and BPM without owning UI state.
Preferred boundary:

- expose weak UI/library hooks similar to the existing track-key hook;
- production UI/library returns the loaded deck metadata/BPM;
- host tests provide stubs.

The hook must return `NULL` or equivalent when beatgrid is not loaded, causing
the BPM fallback path to run.

## State and Audio Effects

Beat Jump must:

- call `audio_engine_deck_seek(deck, target_ms)`;
- update `deck_state_t.position_ms` after a successful seek;
- leave play/pause state unchanged;
- be deck-local;
- not modify loop state in this slice;
- log a concise `deck N beat jump ±X -> Y ms` message on success.

If seek fails, the event is consumed and P4 logs a warning.

## Tests

Add or update host tests before implementation:

- Shift + Cue/Loop Call Left seeks the requested deck by `-1` beat.
- Shift + Cue/Loop Call Right seeks the requested deck by `+1` beat.
- Beat Jump pad actions map pads 1-8 to `-32,-16,-8,-4,+4,+8,+16,+32`.
- Deck 1 and Deck 2 stay independent.
- Release events do not seek.
- Beatgrid path clamps to edges.
- BPM fallback works when metadata is absent.

Run `.\tests\run_p4_host_tests.ps1` after implementation. Because S3 mapping
already exists and no S3 code is expected to change, S3 tests are only required
if the shared protocol or S3 mapper changes.

## Hardware Smoke

After P4 build/flash:

1. Load a track with beatgrid metadata on Deck 1.
2. Enter Beat Jump mode.
3. Press pads 4 and 5 to verify short backward/forward jumps.
4. Press pads 1 and 8 to verify large backward/forward jumps with clamping.
5. Verify playback state is preserved while playing.
6. Repeat a minimal Deck 2 smoke if time allows.

Record results in:

- `docs/DDJ_FLX4_MIDI_MAP.md`
- `docs/validation/FLX4_LED_MIDI_OUT_CAPTURE.md` if LED/reconnect observations
  are touched
- `docs/STARTUP_CHECKLIST.md` if the deferred status changes materially
