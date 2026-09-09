# ESP-IDF 6.0.2 P4 Acceptance Status

Status: **active P4-only migration acceptance, reconciled 2026-09-09**.

The full historical migration record is
[`ARCHIVE_ESP_IDF_6_0_2_MIGRATION.md`](ARCHIVE_ESP_IDF_6_0_2_MIGRATION.md).
The active firmware is pinned to ESP-IDF v6.0.2 and has no alternate supported
SDK.

## Software gates

- [x] P4 project resolves managed components reproducibly from committed
  `dependencies.lock`.
- [x] Complete P4 host suite passes on Windows PowerShell.
- [x] Headless LVGL exact-screenshot gate passes for the implemented UI.
- [x] Clean P4 `build_signed` passes under ESP-IDF v6.0.2.
- [x] Signed OTA packaging and verification pass.
- [x] Current `RC2-114-gc8b2711` source candidate passes automated/build gates
  and signed-package verification.
- [x] Exact `RC2-113-gaf597d8` image boots from the opposite OTA slot.
- [ ] Install and focused-smoke `RC2-114-gc8b2711` on P4 hardware.

## Hardware gates already demonstrated

- [x] P4 display, touch, PSRAM-backed UI, Settings and paginated Library.
- [x] USB0 Rekordbox media and real MP3 playback.
- [x] Direct USB1 FLX4 profile, MIDI, LEDs and four-channel UAC.
- [x] PCM5102A MAIN and direct FLX4 cue path.
- [x] One USB0 remove/reinsert with FLX4 retained.
- [x] One FLX4 reconnect with USB0 retained and post-reconnect dual playback.
- [x] Thirty-minute exact-image dual-active MP3 seek/restart soak.
- [x] First remote PLAY after the two-minute screensaver timeout.
- [x] Playback-scoped UAC data-loss reporting on the exact image.

## Hardware gates still open

- [ ] Measured protected and backfeed-free 5 V/VBUS topology.
- [ ] Cold/warm boot, both insertion orders and repeated USB0/USB1 lifecycle
  matrix.
- [ ] USB0 removal during active load/decode and hands-free reboot recovery.
- [ ] Verified physical WAV/FLAC fixtures and sustained mixed-format cache load.
- [ ] BNA and locked-backend-read counter acceptance under sustained playback.
- [ ] Worst-case dual Master Tempo P4 CPU/I2S and listening acceptance.
- [ ] Detailed near-EOF, scratch/re-grab, STOP/reload and Beat FX matrix.
- [ ] Hardened pull OTA, negative/slow/interrupted upload and rollback smoke.
- [ ] Multi-hour combined-load run.
- [ ] Closed-enclosure power, thermal, RF and wired-recovery acceptance.

A green build is not hardware acceptance. Work these rows through
[`P4_DUAL_USB_NEXT_SESSION.md`](P4_DUAL_USB_NEXT_SESSION.md).
