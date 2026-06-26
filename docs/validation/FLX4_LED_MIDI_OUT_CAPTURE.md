# FLX4 LED MIDI OUT Capture Runbook

Status: MVP reconnect resynchronization verified. Phase 7 LED addresses are
seeded from the verified Mixxx XML and then promoted with host tests plus
hardware smoke/capture notes recorded here.

Purpose: record MIDI OUT messages sent to the DDJ-FLX4 for every LED that
Phase 7 may drive from P4-owned state. XML-derived rows may be implemented
when covered by S3/P4 host tests, but each delivered LED group still needs a
hardware acceptance note here; reconnect behavior is tracked separately.

## Required Setup

- DDJ-FLX4 connected to a desktop MIDI monitor or Mixxx session capable of
  observing all MIDI OUT messages sent to the controller.
- Current repository branch and commit recorded in the capture notes.
- S3 and P4 firmware versions recorded if firmware participates in the test.
- Fresh controller power cycle before the first capture run.
- Exit the controller's startup demo illumination by pressing a physical
  control before evaluating individual LED probes.

## Capture Rules

- Capture off, on, blink, and alternate color values separately where the LED
  supports them.
- Record the full three-byte MIDI message: USB status byte, note or CC number,
  and value.
- Record whether Mixxx or the controller requires periodic refresh to keep the
  LED state active.
- Record reconnect behavior after USB removal/reinsert and after S3 reset.
- Leave LEDs that do not physically exist as `not present`, not blank.
- If a value is inferred from XML but not observed on hardware, mark it
  `xml candidate only` and keep it out of production firmware.

## Capture Matrix

| Control LED | Deck | P4 owner/state | USB status | Note/CC | Off value | On value | Blink/alternate value | Refresh required | Verification status | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Play/Pause | 1 | deck_core playing | `0x90` | note `0x0B` | `0x00` | `0x7F` green | pending | pending | physically verified 2026-06-20 | S3 probe console sent off/on/off; Deck 1 PLAY LED followed each value. |
| Play/Pause | 2 | deck_core playing | captured in MVP | captured in MVP | captured in MVP | captured in MVP | captured in MVP | captured in MVP | verified MVP | Reconfirm during the extended LED reconnect sweep. |
| Cue | 1 | deck_core cue state | captured in MVP | captured in MVP | captured in MVP | captured in MVP | captured in MVP | captured in MVP | verified MVP | Reconfirm during the extended LED reconnect sweep. |
| Cue | 2 | deck_core cue state | captured in MVP | captured in MVP | captured in MVP | captured in MVP | captured in MVP | captured in MVP | verified MVP | Reconfirm during the extended LED reconnect sweep. |
| Headphone Cue/PFL | 1 | audio_engine monitoring | captured in MVP | captured in MVP | captured in MVP | captured in MVP | captured in MVP | captured in MVP | verified MVP | Reconfirm during the extended LED reconnect sweep. |
| Headphone Cue/PFL | 2 | audio_engine monitoring | captured in MVP | captured in MVP | captured in MVP | captured in MVP | captured in MVP | captured in MVP | verified MVP | Reconfirm during the extended LED reconnect sweep. |
| Vinyl | 1 | deck_core vinyl mode | not present | not present | not present | not present | not present | not applicable | physically inspected 2026-06-20 | FLX4 has no dedicated Vinyl button/LED; Vinyl behavior defaults enabled. |
| Vinyl | 2 | deck_core vinyl mode | not present | not present | not present | not present | not present | not applicable | physically inspected 2026-06-20 | FLX4 has no dedicated Vinyl button/LED; Vinyl behavior defaults enabled. |
| Sync enabled | 1 | beat_sync controller | `0x90` | note `0x58` | `0x00` | `0x7F` | pending | pending | physically verified 2026-06-20 | BEAT SYNC Deck 1 LED followed off/on/off probe. |
| Sync enabled | 2 | beat_sync controller | `0x91` | note `0x58` | `0x00` | `0x7F` | pending | pending | physically verified 2026-06-20 | BEAT SYNC Deck 2 LED followed off/on/off probe. |
| Master | 1 | beat_sync master owner | not present | not present | not present | not present | not present | not applicable | physically inspected 2026-06-20 | MASTER is a long-press label on the BEAT SYNC control, without a dedicated LED; `0x90/0x5C` produced no indication. |
| Master | 2 | beat_sync master owner | not present | not present | not present | not present | not present | not applicable | physically inspected 2026-06-20 | MASTER is a long-press label on the BEAT SYNC control, without a dedicated LED; no separate output mapping required. |
| Quantize | 1 | deck quantize state | not present | not present | not present | not present | not present | not applicable | physically inspected 2026-06-20 | No dedicated FLX4 Quantize button/LED; function remains in P4 UI. |
| Quantize | 2 | deck quantize state | not present | not present | not present | not present | not present | not applicable | physically inspected 2026-06-20 | No dedicated FLX4 Quantize button/LED; function remains in P4 UI. |
| Loop In | 1 | deck loop state | `0x90` | note `0x10` | `0x00` | `0x7F` | pending | pending | physically verified 2026-06-20 | IN/4BEAT Deck 1 LED followed off/on/off probe. |
| Loop In | 2 | deck loop state | `0x91` | note `0x10` | `0x00` | `0x7F` | pending | pending | channel-pair accepted 2026-06-20 | Accepted from physically verified Deck 1 address and established Deck 2 status-channel pattern. |
| Loop Out | 1 | deck loop state | `0x90` | note `0x11` | `0x00` | `0x7F` | pending | pending | physically verified 2026-06-20 | OUT Deck 1 LED followed off/on/off probe. |
| Loop Out | 2 | deck loop state | `0x91` | note `0x11` | `0x00` | `0x7F` | pending | pending | channel-pair accepted 2026-06-20 | Accepted from physically verified Deck 1 address and established Deck 2 status-channel pattern. |
| Reloop/Exit | 1 | deck loop state | not present | not present | not present | not present | not present | not applicable | physically inspected 2026-06-20 | RELOOP/EXIT control has no physical LED; `0x90/0x4D` produced no indication. |
| Reloop/Exit | 2 | deck loop state | not present | not present | not present | not present | not present | not applicable | physically inspected 2026-06-20 | RELOOP/EXIT control has no physical LED; no output mapping required. |
| Hot Cue mode | 1 | performance mode | `0x90` | note `0x1B` | `0x00` | `0x7F` | pending | pending | physically verified 2026-06-20 | HOT CUE mode LED followed off/on/off probe. |
| Hot Cue mode | 2 | performance mode | `0x91` | note `0x1B` | `0x00` | `0x7F` | pending | pending | physically verified 2026-06-20 | HOT CUE mode LED followed off/on/off probe. |
| Beat Jump mode | 1 | performance mode | `0x90` | note `0x20` | `0x00` | `0x7F` | pending | pending | physically verified 2026-06-20 | BEAT JUMP mode LED followed off/on/off probe. |
| Beat Jump mode | 2 | performance mode | `0x91` | note `0x20` | `0x00` | `0x7F` | pending | pending | physically verified 2026-06-20 | BEAT JUMP mode LED followed off/on/off probe. |
| Sampler mode | 1 | performance mode / sampler UI | `0x90` | note `0x22` | `0x00` | `0x7F` | pending | pending | physically verified 2026-06-20 | SAMPLER mode LED followed off/on/off probe; replaces Key Shift UI scope. |
| Sampler mode | 2 | performance mode / sampler UI | `0x91` | note `0x22` | `0x00` | `0x7F` | pending | pending | physically verified 2026-06-20 | SAMPLER mode LED followed off/on/off probe; replaces Key Shift UI scope. |
| Beat Loop mode | 1 | performance mode | `0x90` | note `0x6D` | `0x00` | `0x7F` | pending | pending | physically verified 2026-06-20 | BEAT LOOP mode LED followed off/on/off probe. |
| Beat Loop mode | 2 | performance mode | `0x91` | note `0x6D` | `0x00` | `0x7F` | pending | pending | physically verified 2026-06-20 | BEAT LOOP mode LED followed off/on/off probe. |
| Performance pads | 1 | cue/loop/pad/sampler state | pending | pending | pending | pending | pending | pending | XML candidate / acceptance pending | Verify each implemented pad LED group separately. |
| Performance pads | 2 | cue/loop/pad/sampler state | pending | pending | pending | pending | pending | pending | XML candidate / acceptance pending | Verify each implemented pad LED group separately. |
| Beat FX On/Off | global/deck | audio_fx rack | `0x94` | note `0x47` | `0x00` | `0x7F` | pending | pending | physically verified 2026-06-20 | Beat FX ON/OFF LED followed off/on/off probe. |
| Pad FX1 mode | 1 | deck FX mode / rack | `0x90` | note `0x1E` | `0x00` | `0x7F` | pending | pending | physically verified 2026-06-20 | PAD FX1 mode LED followed off/on/off probe. |
| Pad FX1 mode | 2 | deck FX mode / rack | `0x91` | note `0x1E` | `0x00` | `0x7F` | pending | pending | channel-pair accepted 2026-06-20 | Accepted from physically verified Deck 1 address and established Deck 2 status-channel pattern. |
| Pad FX2 mode | 1 | deck FX mode / rack | `0x90` | note `0x6B` | `0x00` | `0x7F` blinking | blinking | pending | physically verified 2026-06-20 | Shift/PAD FX2 uses the physical PAD FX LED in blinking mode. |
| Pad FX2 mode | 2 | deck FX mode / rack | `0x91` | note `0x6B` | `0x00` | `0x7F` blinking | blinking | pending | channel-pair accepted 2026-06-20 | Accepted from physically verified Deck 1 address and established Deck 2 status-channel pattern. |
| Smart CFX | global/deck | smart_cfx router | `0x96` | note `0x00` | `0x00` | `0x7F` | pending | pending | physically verified 2026-06-20 | S3 probe console sent off/on/off after exiting demo illumination; SMART CFX LED followed each value. |
| Smart Fader | global | smart_fader controller | `0x96` | note `0x01` | `0x00` | `0x7F` | pending | pending | physically verified 2026-06-20 | S3 probe console sent off/on/off after exiting demo illumination; SMART FADER LED followed each value. |

## Reconnect Sweep

For each captured LED group, repeat these scenarios and record pass/fail in the
notes:

Control-link wiring was physically verified on 2026-06-20 with S3 translator
firmware and P4 firmware `fe1792f-dirty`: P4 Settings reported
`Control Link (S3): Connected`.

The first end-to-end transport attempt exposed an S3 reset after the P4 sent
an LED frame. The decoded backtrace ended in `panel_led_set()` calling
`xQueueSemaphoreTake()` with the legacy panel mutex unset because FLX4
translator mode does not initialize `panel_io`. The panel LED API now treats
pre-initialization calls as no-ops and has a host regression test.

After flashing the fix, S3 remained stable through boot, FLX4 enumeration,
P4 LED traffic, and heartbeat operation. A physical Deck 1 transport smoke
test then produced:

- `0x90 0x0B 0x7F` -> P4 `deck 1 play -> PLAYING`;
- `0x90 0x0B 0x00` release received;
- second `0x90 0x0B 0x7F` -> P4 `deck 1 play -> PAUSED`;
- second `0x90 0x0B 0x00` release received.

This confirms the FLX4 -> S3 translator -> P4 Play/Pause path and removes the
S3 crash that previously made the second press appear ineffective.

| Scenario | Expected result | Result | Notes |
| --- | --- | --- | --- |
| USB unplug/reinsert while stopped | P4-confirmed snapshot is restored | pass for transport CUE D1/D2 2026-06-20 | Both decks were paused with their transport CUE LED active. After separate reconnect runs, each CUE LED returned automatically and its deck remained stopped. P4 logged a fresh forced snapshot after each reconnect. Extended Phase 7 LED groups remain outside this MVP result. |
| USB unplug/reinsert while playing | P4-confirmed snapshot is restored without changing playback state | pass for Play/PFL D1/D2 2026-06-20 | Both decks were exercised while playing with headphone CUE/PFL active. Playback continued without interruption and PLAY/PFL LEDs returned without a button press. Captured logs show S3 disconnect/connect publications followed by P4 `FLX4 connected; LED snapshot requested` and `forcing complete FLX4 LED snapshot`. |
| S3 reset while P4 keeps state | Full snapshot is resent after heartbeat recovery | pass for Play D1 2026-06-20 | S3 was hard-reset over COM3 while P4 kept Deck 1 playing. Playback continued and the PLAY LED returned automatically. P4 logged a new connection-state request and forced snapshot after S3 re-enumerated the controller. |
| P4 reset while S3 stays connected | LEDs return to P4 boot/default state | pass 2026-06-20 | P4 was reset over COM15 while Deck 1 was playing and its PLAY LED was on. P4 rebooted cleanly, returned to its empty/paused boot state, and the physical Deck 1 PLAY LED turned off. S3 remained stable. |
| Duplicate LED frame | S3 suppresses duplicate raw output after successful send | host-tested 2026-06-20 | `flx4_led_snapshot` verifies normal diff publication suppresses unchanged values after a successful send. |
| Dropped LED frame | Next forced snapshot corrects the controller | host-tested 2026-06-20 | `flx4_led_snapshot` keeps failed sends invalid for retry, and a forced reconnect snapshot republishes all six MVP LED values including off states. |

## Salvage Branch Hardware Verification

Verification date: 2026-06-20

Firmware branch: `codex/flx4-extended-controls-salvage`

Firmware commit: `b848b5c`

Merged-to-master commit: `9df574c`

COM ports:

- S3: COM3
- P4: COM15

Result: pass, confirmed by operator.

Observed behavior:

- S3 firmware published DDJ-FLX4 USB connection state from the USB MIDI host
  lifecycle.
- P4 firmware handled the connected state by forcing a P4-owned LED snapshot.
- FLX4 reconnect restored the MVP LED state without changing playback or deck
  state.
- The implemented forced snapshot scope is intentionally limited to the MVP
  LED set: Deck 1/2 Play, Cue, and PFL.

## Phase 7 Pad Mode, Beat Sync, and Loop LED Smoke Checklist

Status: firmware implemented, software-tested, build-verified. Pad-mode,
Beat Sync, Loop In/Out, and Hot Cue behavior smoke have partial hardware pass
coverage as recorded below. S3 reset recovery after an extended Phase 7 LED
snapshot is fixed and smoke-verified as of 2026-06-26; full manual FLX4 USB
replug LED-state acceptance passed on 2026-06-26.

Firmware branch: `codex/phase7-extended-controls-vu`

Scope:

- P4 owns `deck_core.pad_mode` and emits one selected pad-mode LED per deck.
- P4 owns `deck_core.sync_enabled` as a placeholder Beat Sync toggle until the
  real beat-sync engine exists.
- P4 owns active-loop LED feedback through `audio_engine_deck_get_loop_state()`;
  active loops light both Loop In and Loop Out LEDs on the affected deck.
- S3 maps P4 LED frames to XML-derived FLX4 MIDI OUT messages.

Run this checklist after a fresh S3/P4 flash and FLX4 power cycle:

| Step | Action | Expected result | Result | Notes |
| --- | --- | --- | --- | --- |
| 1 | Press Deck 1 `HOT CUE` | Deck 1 Hot Cue mode LED on; other Deck 1 mode LEDs off | pass 2026-06-21 | Operator confirmed LEDs followed selected state. |
| 2 | Press Deck 1 `PAD FX1` | Deck 1 Pad FX1 mode LED on; other Deck 1 mode LEDs off | pass 2026-06-21 | Operator confirmed LEDs followed selected state. |
| 3 | Press Deck 1 `BEAT JUMP` | Deck 1 Beat Jump mode LED on; other Deck 1 mode LEDs off | pass 2026-06-21 | Operator confirmed LEDs followed selected state. |
| 4 | Press Deck 1 `SAMPLER` | Deck 1 Sampler mode LED on; other Deck 1 mode LEDs off | pass 2026-06-21 | Operator confirmed LEDs followed selected state. |
| 5 | Press Deck 1 shifted secondary modes | Keyboard, Pad FX2, Beat Loop, and Key Shift LEDs follow the selected shifted mode | pass 2026-06-21 | Use Shift + matching physical mode button; operator confirmed LEDs followed selected state. |
| 6 | Repeat steps 1-5 on Deck 2 | Deck 2 LEDs follow independently from Deck 1 | pass 2026-06-21 | Operator confirmed LEDs followed selected state. |
| 7 | Press Deck 1 `BEAT SYNC` once | Deck 1 Beat Sync LED turns on | pass 2026-06-21 | Placeholder state only; tempo/audio must not change. |
| 8 | Press Deck 1 `BEAT SYNC` again | Deck 1 Beat Sync LED turns off | pass 2026-06-21 | Placeholder state only; tempo/audio must not change. |
| 9 | Repeat steps 7-8 on Deck 2 | Deck 2 Beat Sync LED toggles independently from Deck 1 | pass 2026-06-21 | Operator confirmed LEDs followed selected state. |
| 10 | Create or load an active Deck 1 loop from the P4 UI/API | Deck 1 Loop In and Loop Out LEDs turn on | pass 2026-06-21 | Current firmware uses one active-loop state for both LEDs. |
| 11 | Clear the Deck 1 loop | Deck 1 Loop In and Loop Out LEDs turn off | pass 2026-06-21 | |
| 12 | Repeat steps 10-11 on Deck 2 | Deck 2 Loop In/Out LEDs follow independently from Deck 1 | pass 2026-06-21 | |
| 13 | Press Deck 1 `LOOP IN`, advance playback, then press `LOOP OUT` | Deck 1 enters active loop and Loop In/Out LEDs turn on | pass 2026-06-21 | P4 log captured `deck 1 loop in`, `deck 1 loop set`, and FLX4 LED snapshot publication; operator confirmed LEDs followed state. |
| 14 | Press Deck 1 `RELOOP/EXIT` while loop is active | Active loop clears and Loop In/Out LEDs turn off | pass 2026-06-21 | P4 log captured `deck 1 loop exit`; operator confirmed LEDs followed state. |
| 15 | Press Deck 1 `RELOOP/EXIT` again | Last loop is restored and Loop In/Out LEDs turn on | pass 2026-06-21 | P4 log captured restored loop with the previous in/out boundaries. |
| 16 | Press Deck 1 loop halve/double buttons while loop is active | Active loop length halves/doubles without changing loop start | pass 2026-06-21 | One output late diagnostic appeared on a very short Deck 1 loop (`late_max=15668 us`); audio remained running and this is tracked as a watch item. |
| 17 | Repeat steps 13-16 on Deck 2 | Deck 2 loop behavior and LEDs follow independently from Deck 1 | pass 2026-06-21 | P4 log captured independent Deck 2 loop set/exit/restore/halve events. |
| 18 | With non-default pad mode, Beat Sync enabled, and active loop state, unplug/reinsert FLX4 USB | P4 reconnect snapshot restores selected pad mode LED, Beat Sync LED, and Loop In/Out LEDs | pass 2026-06-26 | S3 logged FLX4 disconnect/reconnect, P4 forced an LED snapshot, no S3 stack overflow/reboot loop occurred, and operator confirmed LEDs returned. Playback/deck state did not change. |
| 19 | With non-default pad mode, Beat Sync enabled, and active loop state, reset S3 only | P4 reconnect snapshot restores selected pad mode LED, Beat Sync LED, and Loop In/Out LEDs after S3 recovery | pass 2026-06-26 | Regression fixed: S3 no longer stack-overflows in `ctrl_rx` during the extended LED snapshot, S3 re-enumerated FLX4, and operator confirmed the controller became responsive again. |

Loop In/Out LED behavior note from 2026-06-26 acceptance: pressing `IN` alone
sets the loop-in marker but does not light the physical Loop In LED. After
pressing `OUT`, the loop becomes active and both Loop In and Loop Out LEDs turn
on. This matches the current P4-owned LED model, where both LEDs are driven
from `audio_engine` active-loop state rather than from a pending loop-in marker.

### 2026-06-26 Extended LED Snapshot Regression

Hardware smoke on `master` commit `eee2e90` exposed an S3 reboot loop during
extended reconnect snapshot publication:

- P4 remained stable and repeatedly logged `FLX4 connected; forcing LED
  snapshot` followed by `forced FLX4 LED snapshot published`.
- S3 published `FLX4 connection state: connected`, then logged repeated
  `MIDI OUT queue full, dropping packet`, then crashed with `stack overflow in
  task ctrl_rx`.
- Root cause: the Phase 7 forced snapshot can publish 44 non-VU LED packets
  for two decks. The S3 MIDI OUT queue capacity was 32 packets, and the
  full-queue warning path ran synchronously in the 2048-byte `ctrl_rx` task.

Fix flashed and smoke-verified on 2026-06-26:

- S3 MIDI OUT queue capacity raised to 64 packets and covered by a host test
  sized against the Phase 7 forced snapshot.
- `ctrl_rx` task stack raised to 4096 bytes and statically guarded by the S3
  host regression runner.
- Full-queue warning log is rate-limited so a transient backlog cannot create
  a log storm on the UART RX task.
- Post-fix S3 reset capture showed boot, USB MIDI endpoint registration,
  `listening for raw USB-MIDI packets`, and `published FLX4 connection state:
  connected` with no stack overflow, no reboot loop, and no MIDI OUT
  queue-full spam. Operator confirmed the controller was responsive after the
  recovery.

## Phase 7 Hot Cue Pad Behavior Smoke

Firmware: P4 app version `b0858e2` on branch
`codex/phase7-extended-controls-vu`.

Status: Deck 1 set/recall/shift-clear behavior passed on hardware
2026-06-21. Deck 2 shifted Hot Cue clear behavior passed on hardware
2026-06-26.

Observed P4 log evidence:

- Track loaded on Deck 1 with track key `0x000000A3`.
- `deck 1 pad mode -> HOT_CUE`
- `deck 1 hot cue 1 set -> 5932 ms`
- `deck 1 hot cue 2 set -> 5346 ms`
- `deck 1 hot cue 2 recall -> 5346 ms`
- `Index seek 5346 ms -> frame 205/9078`
- `deck 1 hot cue 1 recall -> 5932 ms`
- `Index seek 5932 ms -> frame 227/9078`
- `deck 1 hot cue 1 cleared`
- `deck 1 hot cue 1 set -> 26870 ms`
- `Index seek 26870 ms -> frame 1029/9078`

Operator note: Shift + `HOT CUE` mode button selects `KEYBOARD` mode; shifted
Hot Cue clear requires staying in `HOT_CUE` mode and pressing Shift + the pad
itself.

## Promotion Gate

Before adding or enabling a production entry in `flx4_led_map()`:

1. The row above has either physically captured raw values or XML-derived
   values from `docs/reference/Pioneer-DDJ-FLX4.midi.xml` that are covered by
   host tests.
2. A table-driven S3 host test asserts the exact three-byte MIDI output.
3. A P4 snapshot test proves the semantic LED is derived only from P4-owned
   state.
4. Hardware smoke/capture status is recorded, or explicitly marked pending for
   post-implementation acceptance.
5. Reconnect behavior is captured or explicitly marked pending/unsupported.
6. `docs/DDJ_FLX4_MIDI_MAP.md` is updated with the current implementation and
   verification status.
