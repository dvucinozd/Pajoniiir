# FLX4 Input State Snapshot Design

Document status (2026-07-13): implemented reconnect/state-snapshot design.

## Goal

When the ESP32-P4 boots or reconnects while the DDJ-FLX4 and ESP32-S3 remain powered, the P4 should receive the last known physical state of absolute controller inputs without requiring the operator to move each knob or fader first.

## Scope

Implement S3-to-P4 replay for known absolute input values only:

- Channel faders: CH1, CH2
- Crossfader
- Trim / pregain: CH1, CH2
- EQ high/mid/low: CH1, CH2
- Channel filter / CFX knob raw values: CH1, CH2
- Master volume
- Headphones mix
- Beat FX depth

Do not replay tempo faders in this phase. The P4 currently treats live tempo input as a user action that disables Beat Sync; replaying tempo during reconnect could incorrectly change sync state.

Do not replay buttons, pad actions, toggle controls, browse encoder deltas, jog deltas, or PFL. Replaying momentary or toggle inputs can create false user actions.

## Architecture

The S3 already converts DDJ-FLX4 USB MIDI messages into semantic `control_link` events and keeps partial 14-bit MSB/LSB state in `flx4_map_state_t`. This feature extends that mapper with an explicit snapshot iterator that emits only fully-known absolute controls.

The S3 translator task continues to send live events normally. After FLX4 connection refresh is published, the S3 replays the known snapshot as ordinary `CTRL_TYPE_PITCH` semantic events for the scoped absolute controls. P4 already applies these event IDs through existing mixer/audio handlers, so no P4 protocol expansion is required for this first phase.

Unknown controls are not sent. The S3 must never fabricate center/default values for controls that have not produced a complete value.

## Data flow

1. FLX4 sends USB MIDI CC messages.
2. S3 `flx4_map_message()` updates 14-bit or 7-bit state and emits live events.
3. S3 stores enough validity state to know whether a scoped absolute control has a complete known value.
4. On heartbeat-driven FLX4 connection refresh, S3 publishes `CTRL_ID_FLX4_CONNECTION = connected`.
5. Immediately after a successful refresh, S3 iterates the snapshot and sends known absolute values to P4.
6. P4 applies those values through existing audio/mixer handlers.

## Error handling and safety

- Snapshot replay is best-effort. A UART send failure increments existing send diagnostics; it does not block the translator task.
- Replay must be bounded and low-rate. The maximum snapshot size is small enough to send inline after the 5 s heartbeat refresh.
- Snapshot replay uses only known values and never toggles stateful buttons.
- Beat FX target buttons are not replayed because they are stateful button-derived controls, not absolute knobs.

## Testing

Host tests should cover:

- A 14-bit control becomes snapshot-visible only after both MSB and LSB are observed.
- Snapshot iteration emits scoped absolute controls and excludes tempo, jog, browse, buttons, PFL, and unknown controls.
- Beat FX depth is snapshot-visible as a 7-bit absolute value.
- Snapshot replay order is deterministic enough for tests and debugging.

Firmware verification should run the S3 host tests and at least the S3 firmware build. Because the P4 applies existing event IDs and no P4 code change is planned for the first phase, P4 build is optional unless shared headers or docs are changed in a way that affects it.
