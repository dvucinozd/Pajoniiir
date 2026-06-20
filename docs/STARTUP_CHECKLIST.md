# Startup Checklist

## Repository

- [x] Start from a fork-style import of `dvucinozd/CDJ100S-XXX`.
- [x] Preserve upstream README in `docs/reference/CDJ100S-XXX-README.md`.
- [x] Add `docs/reference/Pioneer-DDJ-FLX4.midi.xml`.
- [x] Commit the baseline import and DDJ-FFL4 documentation.

## Local Tooling

- [x] Confirm ESP-IDF v5.5 is installed.
- [x] Confirm `Initialize-Idf.ps1` works in PowerShell.
- [x] Confirm `idf.py --version`.
- [x] Confirm MinGW/GCC is available for PC tests.
- [x] Use `tests/run_s3_host_tests.ps1` for S3 host regressions when `make`
  is not present in PATH.
- [x] Use `tests/run_p4_host_tests.ps1` for P4 host regressions when `make`
  is not present in PATH.

## Baseline Builds

- [x] Build `firmware/control-board-s3`.
- [x] Build `firmware/main-deck-p4`.
- [x] Run inherited PC tests that do not require hardware.

## Hardware Bring-Up

- [x] Confirm S3 serial port (`COM3` on 2026-06-08).
- [x] Confirm P4 serial port (`COM15` on 2026-06-13).
- [x] Flash S3 FLX4 host-mode firmware (`fd663e6`) before FLX4 capture.
- [x] Flash P4 firmware after dual-deck UI stabilization (`5f9b425` on 2026-06-13).
- [x] Verify S3/P4 UART heartbeat.
- [x] Validate DDJ-FLX4 physical USB host setup on S3.
- [x] Capture raw MIDI packets for MVP controls.

## Current Repository State

- `master` includes the P4 dual-deck UI refactor, the 2026-06-13 Deck 2
  Overview waveform jitter fix, the S3 review fixes for FLX4 host/translator
  mode, the enabled S3 UART translation configuration, FLX4 reconnect LED
  resynchronization, and raw Smart CFX/Smart Fader input mapping.
- The former `codex/p4-review-fixes` scope is merged: per-deck audio status,
  shared output/codec lifecycle, deck-core lock scope cleanup, high-rate
  control coalescing, source-safe media load, parser hardening, and the P4 host
  regression runner are now part of `master`.
- S3 review fixes include a host regression runner, hardened DDJ-FLX4 USB MIDI
  descriptor handling, deck-aware S3 `control_link` constants, an XML-derived
  FLX4 MIDI mapper, translator-mode UART coalescing, and safer legacy CDJ panel
  queue behavior.
- P4 Overview waveform performance branch adds RGB565 circular-strip scrolling:
  steady main waveform motion should report `UI_OVERVIEW_WAVE_CACHE_OFFSET`
  with zero rendered columns, while occasional edge updates render bounded
  batches instead of moving the whole waveform buffer.
- Current P4 Overview polish keeps title/timer LVGL invalidation bounded,
  stabilizes the blue-strip remaining-time display with fixed timer segments,
  centers beat-match/phase indicators around the main playhead, removes weak
  active-deck accent bars/borders, and uses taller Play/Cue touch targets.
- P4 UI Phase 6 is closed for the local touchscreen path: `ui.c` is now an
  887-line orchestrator, with Overview, Library, Controls, Performance tabs,
  Settings, Status, LVGL backend, renderer, scheduler, and frame-context logic
  split into focused modules.
- S3 DDJ-FLX4 raw MIDI capture and translation are verified and completed.
- S3 and P4 Phase 5 LED feedback (Play, Cue, PFL) is verified and completed.
- S3 publishes FLX4 USB connection state and P4 forces a complete MVP
  Play/Cue/PFL LED snapshot after reconnect; hardware verification passed on
  2026-06-20.
- SMART CFX and SMART FADER raw inputs are mapped as momentary semantic
  press/release events. P4 Smart DSP/settings behavior remains deferred.
- P4 dual-deck audio scheduling is hardware-verified after the 2026-06-20
  preload/output pacing pass: both decks can play with normal audio and normal
  waveform motion.
- P4 captive portal web server and mobile controller interface are stabilized,
  optimized, and completed. P4 starts the hosted Wi-Fi AP directly for this
  path; the old Settings `link_mode` selector has been removed from active
  firmware.

## First Firmware Task

`firmware/control-board-s3/components/flx4_midi_host/` contains the raw
USB MIDI logger and the software translator path. Built with
`CONFIG_DDJ_FLX4_HOST_MODE=y` and `CONFIG_DDJ_FLX4_TRANSLATE_TO_P4=y`
(enabled on 2026-06-14).

## P4 Overview Waveform Smoke Test

- [x] Flash current P4 firmware to COM15. Last confirmed: 2026-06-16 after
  Overview timer/beat/phase polish.
- [ ] Start serial monitor and keep it running for at least 60 seconds while
  Deck 1 and Deck 2 are both loaded and playing.
- [ ] Confirm no panic, watchdog timeout, brownout, or unexpected reset appears
  in the log.
- [ ] Confirm steady logs are mostly `kind=OFFSET` with `cols=0`; `EDGE`
  should appear only occasionally with bounded column counts.
- [ ] Record `cache_us`, `ppa_us`, and `ui_update interval` values in
  `docs/DEVELOPMENT_PLAN.md` after a representative dual-deck run.

S3 status: USB host was successfully brought up on native OTG port. By increasing
`CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE=512`, the large configuration descriptors
of Pioneer DDJ-FLX4 are now successfully parsed. Raw MIDI capture of MVP controls was
verified to match `docs/DDJ_FLX4_MIDI_MAP.md`. Heartbeat and translator tasks are active,
emitting deck-aware `0xA5` control link frames.

Required output from the spike:

- FLX4 device descriptor summary: successfully verified.
- Endpoint/interface summary: interface=4, endpoint=0x82 (MIDIStreaming IN).
- Raw packet logs for every MVP control: verified (Play, Cue, Load, Browse, Faders, Pitch, PFL).
- Differences from `docs/DDJ_FLX4_MIDI_MAP.md`: none found, map is 100% accurate.

After the capture, `CONFIG_DDJ_FLX4_TRANSLATE_TO_P4` was enabled. S3 now successfully emits
deck-aware 7-byte `0xA5` frames while P4 heartbeat detection is supported.

## Next Controller Expansion

- [x] Browse press (`0x96/0x41`) is routed end to end as a P4
  Library/Overview toggle. Load 1 and Load 2 remain the only deck-load
  buttons, and Browse rotate moves the selected row one detent at a time.
- [x] MVP Play/Cue/PFL LED reconnect resynchronization is routed end to end
  through S3 FLX4 connection state and P4 forced LED snapshots.
- [x] SMART CFX (`0x96/0x00`) and SMART FADER (`0x96/0x01`) are raw-captured
  and mapped as semantic input-only button events.
- [ ] Build the extended control inventory from the vendored Mixxx XML.
- [ ] Add deck modifiers and transport extensions with P4-owned semantics.
- [ ] Add supported mixer/monitoring controls and 14-bit range tests.
- [ ] Connect FLX4 pad modes to existing P4 Hot Cue, Beat Loop, Beat Jump, and
  Key Shift behavior before adding new performance features.
- [ ] Expand LED feedback only from P4-confirmed state.
- [ ] Hardware-smoke each delivered control group and record exceptions from
  the XML mapping.

See Phase 7 in `docs/DEVELOPMENT_PLAN.md`. XML status/midino values are now the
implementation seed because the physical MVP capture matched them exactly;
Mixxx JavaScript behavior is not imported.
