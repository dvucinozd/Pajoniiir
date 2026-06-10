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

- [x] Confirm S3 serial port (`COM3` on 2026-06-08).
- [ ] Confirm P4 serial port.
- [x] Flash S3 FLX4 host-mode firmware (`fd663e6`) before FLX4 capture.
- [ ] Flash inherited P4 firmware once before dual-deck changes.
- [ ] Verify S3/P4 UART heartbeat.
- [ ] Validate DDJ-FLX4 physical USB host setup on S3 next session.
- [ ] Capture raw MIDI packets for MVP controls.

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
