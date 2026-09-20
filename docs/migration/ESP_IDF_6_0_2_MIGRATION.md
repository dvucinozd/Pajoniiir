# ESP-IDF 6.0.2 P4 Acceptance Status

Status: **active P4-only migration acceptance, reconciled 2026-09-20**.

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
- [x] Validated `RC2-116-g77d723c` firmware checkpoint passes automated/build
  gates and signed-package verification.
- [x] Exact `RC2-116-g77d723c` image installs and boots from `ota_1` through
  signed OTA.
- [x] Targeted three-hour continuous dual-MP3 limiter/WDT soak passes with one
  boot epoch and no watchdog reset, PCM underrun or active UAC loss and no
  observable USB/controller/output failure.

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
- [x] Targeted three-hour limiter/WDT regression on `RC2-116-g77d723c`.
- [x] Current bench protected and backfeed-free 5 V/dual-VBUS measurement gate
  passed by operator report.

## Final hardware disposition

- [x] Dual-USB lifecycle matrix accounted: 43 PASS, seven explicit I/J waivers
  and zero pending cycles.
- [x] Active-load removal, reboot recovery and post-OTA dual-root recovery.
- [x] Physical MP3/WAV/FLAC and sustained mixed-format cache acceptance.
- [x] BNA/locked-read, dual Master Tempo, near-EOF, scratch, reload and Beat FX
  acceptance within the recorded M2 beta scope.
- [x] Hardened pull OTA, interruption recovery, rollback and multi-hour
  combined-load run.
- [x] Existing enclosure accepted by operator after approximately two months
  of use; wired recovery confirmed. Numeric thermal/RF/strain margins remain
  uncaptured and must be repeated after a physical or power-topology change.

A green build is not hardware acceptance. Work these rows through
[`P4_DUAL_USB_NEXT_SESSION.md`](P4_DUAL_USB_NEXT_SESSION.md).
