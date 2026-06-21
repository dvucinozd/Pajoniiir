# FLX4 LED MIDI OUT Capture Runbook

Status: physical capture in progress; MVP reconnect resynchronization verified.

Purpose: record verified MIDI OUT messages sent to the DDJ-FLX4 for every LED
that Phase 7 may drive from P4-owned state. Do not promote any row to a
production S3 mapping until the raw USB status, address, values, and reconnect
behavior are captured on the physical controller.

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
| Performance pads | 1 | cue/loop/pad/sampler state | pending | pending | pending | pending | pending | pending | raw capture required | Capture each pad index and mode separately. |
| Performance pads | 2 | cue/loop/pad/sampler state | pending | pending | pending | pending | pending | pending | raw capture required | Capture each pad index and mode separately. |
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

## Phase 7 Pad Mode and Beat Sync LED Smoke Checklist

Status: firmware implemented, software-tested, flashed; physical smoke pending.

Firmware branch: `codex/phase7-extended-controls-vu`

Scope:

- P4 owns `deck_core.pad_mode` and emits one selected pad-mode LED per deck.
- P4 owns `deck_core.sync_enabled` as a placeholder Beat Sync toggle until the
  real beat-sync engine exists.
- S3 maps P4 LED frames to XML-derived FLX4 MIDI OUT messages.

Run this checklist after a fresh S3/P4 flash and FLX4 power cycle:

| Step | Action | Expected result | Result | Notes |
| --- | --- | --- | --- | --- |
| 1 | Press Deck 1 `HOT CUE` | Deck 1 Hot Cue mode LED on; other Deck 1 mode LEDs off | pending | |
| 2 | Press Deck 1 `PAD FX1` | Deck 1 Pad FX1 mode LED on; other Deck 1 mode LEDs off | pending | |
| 3 | Press Deck 1 `BEAT JUMP` | Deck 1 Beat Jump mode LED on; other Deck 1 mode LEDs off | pending | |
| 4 | Press Deck 1 `SAMPLER` | Deck 1 Sampler mode LED on; other Deck 1 mode LEDs off | pending | |
| 5 | Press Deck 1 shifted secondary modes | Keyboard, Pad FX2, Beat Loop, and Key Shift LEDs follow the selected shifted mode | pending | Use Shift + matching physical mode button. |
| 6 | Repeat steps 1-5 on Deck 2 | Deck 2 LEDs follow independently from Deck 1 | pending | |
| 7 | Press Deck 1 `BEAT SYNC` once | Deck 1 Beat Sync LED turns on | pending | Placeholder state only; tempo/audio must not change. |
| 8 | Press Deck 1 `BEAT SYNC` again | Deck 1 Beat Sync LED turns off | pending | |
| 9 | Repeat steps 7-8 on Deck 2 | Deck 2 Beat Sync LED toggles independently from Deck 1 | pending | |
| 10 | With non-default pad mode and Beat Sync enabled, unplug/reinsert FLX4 USB | P4 reconnect snapshot restores selected pad mode LED and Beat Sync LED | pending | Playback/deck state must not change. |
| 11 | With non-default pad mode and Beat Sync enabled, reset S3 only | P4 reconnect snapshot restores selected pad mode LED and Beat Sync LED after S3 recovery | pending | P4 state remains authoritative. |

## Promotion Gate

Before adding or enabling a production entry in `flx4_led_map()`:

1. The row above has physical raw values, not XML-only candidates.
2. A table-driven S3 host test asserts the exact three-byte MIDI output.
3. A P4 snapshot test proves the semantic LED is derived only from P4-owned
   state.
4. Reconnect behavior is captured or explicitly marked unsupported.
5. `docs/DDJ_FLX4_MIDI_MAP.md` is updated with the verified values.
