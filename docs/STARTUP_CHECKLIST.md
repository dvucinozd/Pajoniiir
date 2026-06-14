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
- [ ] Verify S3/P4 UART heartbeat.
- [ ] Validate DDJ-FLX4 physical USB host setup on S3 next session.
- [ ] Capture raw MIDI packets for MVP controls.

## Current Repository State

- `master` includes the P4 dual-deck UI refactor and the 2026-06-13 Deck 2
  Overview waveform jitter fix.
- Branch `codex/p4-review-fixes` adds P4 review fixes: per-deck audio status,
  shared output/codec lifecycle, deck-core lock scope cleanup, high-rate
  control coalescing, source-safe media load, parser hardening, and the P4 host
  regression runner.
- P4 UI Phase 6 is closed for the local touchscreen path: `ui.c` is now an
  887-line orchestrator, with Overview, Library, Controls, Performance tabs,
  Settings, Status, LVGL backend, renderer, scheduler, and frame-context logic
  split into focused modules.
- The next hardware-critical task is still S3 DDJ-FLX4 raw MIDI capture.

## First Firmware Task

`firmware/control-board-s3/components/flx4_midi_host/` now contains the raw
USB MIDI logging spike. Build with `CONFIG_DDJ_FLX4_HOST_MODE=y`, flash the S3,
connect the DDJ-FLX4, and capture the serial logs.

Current S3 status: firmware boots and starts the USB host logger, but FLX4
enumeration was not observed on 2026-06-08. Continue this validation next time
with a powered hub / verified 5 V VBUS / correct S3 OTG host port. P4 firmware
exploration can proceed while this hardware validation remains open.

Required output from the spike:

- FLX4 device descriptor summary.
- Endpoint/interface summary.
- Raw packet logs for every MVP control.
- Differences from `docs/DDJ_FLX4_MIDI_MAP.md`.
