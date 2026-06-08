# Startup Checklist

## Repository

- [x] Start from a fork-style import of `dvucinozd/CDJ100S-XXX`.
- [x] Preserve upstream README in `docs/reference/CDJ100S-XXX-README.md`.
- [x] Add `docs/reference/Pioneer-DDJ-FLX4.midi.xml`.
- [ ] Commit the baseline import and DDJ-FFL4 documentation.

## Local Tooling

- [ ] Confirm ESP-IDF v5.5 is installed.
- [ ] Confirm `Initialize-Idf.ps1` works in PowerShell.
- [ ] Confirm `idf.py --version`.
- [ ] Confirm MinGW `mingw32-make` is available for PC tests.

## Baseline Builds

- [ ] Build `firmware/control-board-s3`.
- [ ] Build `firmware/main-deck-p4`.
- [ ] Run inherited PC tests that do not require hardware.

## Hardware Bring-Up

- [ ] Confirm S3 serial port.
- [ ] Confirm P4 serial port.
- [ ] Flash inherited S3 firmware once before FLX4 changes.
- [ ] Flash inherited P4 firmware once before dual-deck changes.
- [ ] Verify S3/P4 UART heartbeat.
- [ ] Connect DDJ-FLX4 to S3 USB host setup.
- [ ] Capture raw MIDI packets for MVP controls.

## First Firmware Task

`firmware/control-board-s3/components/flx4_midi_host/` now contains the raw
USB MIDI logging spike. Build with `CONFIG_DDJ_FLX4_HOST_MODE=y`, flash the S3,
connect the DDJ-FLX4, and capture the serial logs before changing P4 behavior.

Required output from the spike:

- FLX4 device descriptor summary.
- Endpoint/interface summary.
- Raw packet logs for every MVP control.
- Differences from `docs/DDJ_FLX4_MIDI_MAP.md`.
