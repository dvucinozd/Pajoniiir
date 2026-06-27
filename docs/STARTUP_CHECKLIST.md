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
  resynchronization, raw Smart CFX/Smart Fader input mapping, the P4 splash
  screen port, the official DDJ-FLX4 MIDI message list, and the merged Phase 7
  extended-control surface.
- Phase 7 was merged into `master` and pushed on 2026-06-26. Completed stale
  Codex branches were removed locally and remotely after the merge.
- Remaining non-master branch: `codex/flx4-extended-controls`. It is an older
  dirty experimental Smart/DSP worktree and should be reviewed separately
  before any merge/delete decision.
- The former `codex/p4-review-fixes` scope is merged: per-deck audio status,
  shared output/codec lifecycle, deck-core lock scope cleanup, high-rate
  control coalescing, source-safe media load, parser hardening, and the P4 host
  regression runner are now part of `master`.
- S3 review fixes include a host regression runner, hardened DDJ-FLX4 USB MIDI
  descriptor handling, deck-aware S3 `control_link` constants, an XML-derived
  FLX4 MIDI mapper, translator-mode UART coalescing, and safer legacy CDJ panel
  queue behavior.
- P4 Overview waveform path includes RGB565 circular-strip scrolling:
  steady main waveform motion should report `UI_OVERVIEW_WAVE_CACHE_OFFSET`
  with zero rendered columns, while occasional edge updates render bounded
  batches instead of moving the whole waveform buffer. With UI diagnostics
  enabled, the Overview cache log reports cumulative `FULL`, `OFFSET`, `EDGE`,
  and `NONE` counts plus total rendered columns/blits.
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
- Official DDJ-FLX4 MIDI message list coverage is documented in
  `docs/reference/DDJ-FLX4_MIDI_message_List.md` and cross-referenced from
  `docs/DDJ_FLX4_MIDI_MAP.md`; the Mixxx XML remains the proven authoritative
  input source.
- P4 dual-deck audio scheduling is hardware-verified after the 2026-06-20
  preload/output pacing pass: both decks can play with normal audio and normal
  waveform motion.
- P4 master output now uses a transparent post-sum limiter with lightweight
  limiter telemetry in both the output diagnostic log and the audio mixer
  snapshot. Single-deck level and normal two-deck sums remain unchanged; only
  true int16 overloads are shaped. The P4 status indicator briefly shows
  `CLIP n` when the limiter counter increases.
- S3 USB MIDI host responsiveness was hardware-verified on 2026-06-21 after
  FLX4 VU feedback was made low-priority under USB MIDI OUT queue backlog and
  raw USB MIDI packet logs were demoted to DEBUG in translator mode. Both
  decks can play while controller Play/Pause remains responsive.
- S3 extended LED snapshot recovery was fixed on 2026-06-26 after hardware
  smoke exposed a `ctrl_rx` stack overflow during the wider Phase 7 forced LED
  snapshot. The MIDI OUT queue now covers the full non-VU snapshot burst,
  full-queue warnings are rate-limited, and `ctrl_rx` has a 4096-byte stack.
  Post-fix S3 reset recovery re-enumerated FLX4 and the operator confirmed the
  controller was responsive. Full manual FLX4 USB replug also restored the
  P4-owned LED state without an S3 reboot loop.
- P4 audio output diagnostics were calibrated on 2026-06-21: normal blocking
  `esp_codec_dev_write()` pacing no longer emits per-block `diag output late`
  warnings. A dual-deck hardware run reported zero late warnings while keeping
  aggregate output, limiter, heap, internal SRAM, and PSRAM telemetry. The
  audio engine also exposes these values through a central diagnostics snapshot,
  and `/api/status` mirrors them under `diagnostics` for smoke captures.
- P4 PCM5102A MAIN OUT bring-up passed a 2026-06-27 COM15 measurement with the
  external DAC enabled locally. The PCM5102A I2S1 clock is now reconfigured to
  the loaded track sample rate when the shared output service opens, audio
  loader/decode/output tasks run on CPU0, LVGL remains on CPU1, and a
  dual-deck run reported `late=0 late_max=0 us` with stable ring fill. The
  local `firmware/main-deck-p4/sdkconfig` used for this hardware test is
  intentionally ignored and should not be committed.
- PCM5102A final output acceptance is still pending: test RCA or 3.5 mm TRS
  into an active AUX/LINE IN input. Direct passive TRS headphones on the DAC
  board are not a valid pass/fail signal because the board is a line-out DAC,
  not a headphone amplifier.
- P4 audio engine now exposes a non-boosting software master trim API and
  mixer snapshot field. The Settings tab has a preset button cycling `0 dB`,
  `-3 dB`, and `-6 dB`. Default remains unity, so current playback level is
  unchanged until the operator deliberately lowers it. The selected preset is
  persisted through NVS and reapplied during P4 boot after `audio_engine_init()`.
- P4 firmware defaults now select performance optimization and disable LVGL
  examples/demos. If an ignored local `firmware/main-deck-p4/sdkconfig`
  predates 2026-06-25, regenerate or align it before flashing so it does not
  keep `CONFIG_COMPILER_OPTIMIZATION_DEBUG`.
- P4 now includes the ported LVGL splash screen from the former
  `codex/splash-screen` branch. Boot shows `PajoNiiiR` in `Musieer_80` for
  roughly three seconds, then returns to the already-built main dual-deck UI.
  The `ctrl_rx` UART task stack is 4096 bytes in the same stabilization slice.
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

- [x] Flash current P4 firmware to COM15. Last confirmed: 2026-06-21 after
  P4 audio late-warning threshold calibration.
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
  Hardware smoke on 2026-06-25 verified the Browse path after increasing the
  `deck` task stack to cover the current controller-triggered Library UI call
  chain.
- [x] MVP Play/Cue/PFL LED reconnect resynchronization is routed end to end
  through S3 FLX4 connection state and P4 forced LED snapshots.
- [x] SMART CFX (`0x96/0x00`) and SMART FADER (`0x96/0x01`) are raw-captured
  and mapped as semantic input-only button events.
- [x] Build the extended control inventory from the vendored Mixxx XML.
- [x] Add deck modifiers and transport extensions with P4-owned semantics.
  First slice implemented: Shift, Cue+Shift track-start, Beat Sync, and
  Beat Sync+Shift tempo-range semantic inputs. Cue+Shift has P4 seek-to-start
  behavior; Beat Sync applies one-shot BPM match to the other deck using
  precise ANLZ BPM when available and an internal ±20% safe clamp independent
  of the selected manual Tempo Range. It phase-aligns to the nearest matching
  beat phase only when the target deck is paused and both beatgrids are available. Playing-deck
  phase seek is skipped to avoid audio ring underruns; it does not yet
  continuously follow. Tempo
  Range cycles deck-local `±6%`, `±10%`, and `±16%` fader ranges. Final Phase 7 smoke
  verified loop in/out, reloop/exit, loop halve/double, and beat-jump
  back/forward inputs on both decks. Loop In/Out, Reloop/Exit, loop
  halve/double, normal/shifted Beat Loop pads, and Beat Jump buttons/pads now
  have P4 behavior; Tempo Range hardware behavior smoke passed on 2026-06-25,
  while Beat Loop and Beat Jump hardware behavior smoke remains pending.
- [x] Add supported mixer/monitoring controls and 14-bit range tests.
  Second slice implemented: Trim, EQ high/mid/low, filter, headphone mix,
  loop/beat-jump buttons, pad modes/actions, and P4-driven FLX4 VU meter output
  are mapped/tested in firmware. Hardware capture status is tracked per row in
  `docs/DDJ_FLX4_MIDI_MAP.md`.
  Three-band EQ now has P4 DSP behavior for both decks; trim, filter, and
  headphone-mix remain mapped/state work until their standalone P4 behavior is
  implemented.
- [x] Connect FLX4 pad mode inputs to P4-owned semantic pad mode state.
  The four physical mode buttons and shifted secondary modes are mapped and
  smoke-verified where noted in the MIDI map. Hot Cue pad behavior is
  implemented in P4 for per-track store/recall and shifted clear; Deck 1
  hardware behavior smoke passed on 2026-06-21, and Deck 2 shifted clear smoke
  passed on 2026-06-26.
  Normal and shifted Beat Loop plus Beat Jump pad behavior is implemented in P4
  and remains pending for hardware behavior smoke. Actual Sampler, Key Shift,
  and Pad FX behavior remains a separate P4 feature task.
- [ ] Expand LED feedback only from P4-confirmed state.
  First firmware slice is implemented for P4-owned selected pad mode LEDs
  across direct and shifted modes, Beat Sync enabled state, and
  Loop In/Out LEDs derived from P4 pending loop-in marker and active audio loop
  state. Beat Loop normal pad LED output is implemented from P4 loop state and
  selected Beat Loop pad mode; shifted mirror pad LED output remains deferred.
  Pad-mode, Beat Sync, and active Loop In/Out LED hardware smoke has passed
  where recorded in `docs/validation/FLX4_LED_MIDI_OUT_CAPTURE.md`; full manual
  USB replug LED-state acceptance passed on 2026-06-26. Beat Loop pad LED
  hardware smoke remains pending, S3 reset recovery after the extended reconnect
  snapshot no longer crashes, and P4-only reset recovery is implemented through
  S3 heartbeat connected-state refresh with hardware smoke passed on
  2026-06-26.
- [x] Final hardware-smoke testing of the integrated Phase 7 input surface and record any exceptions from the XML mapping.

See Phase 7 in `docs/DEVELOPMENT_PLAN.md`. XML status/midino values are now the
implementation seed because the physical MVP capture matched them exactly;
Mixxx JavaScript behavior is not imported.
