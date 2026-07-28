# ESP-IDF 6.0.2 migration

This branch migrates both Pajoniiir firmware targets from ESP-IDF 5.5.4 to
ESP-IDF 6.0.2 while keeping functional behavior unchanged until the dual-target
build and existing host regression suites are green.

## Applied changes

- Raised both project CMake minimum versions to 3.22.
- Removed the P4 build-time `ESP_IDF_VERSION=5.5` override.
- Pinned both firmware projects to ESP-IDF 6.0.2.
- Added `espressif/usb` 1.5.0 as the S3/P4 USB Host managed dependency.
- Updated the P4 MSC dependency to `espressif/usb_host_msc` 1.2.0.
- Replaced aggregate `driver` dependencies with explicit GPIO/UART/LEDC driver components.
- Updated the ST7701 dependency to the IDF-6-compatible 2.0.2 release.
- Removed the IDF 5.5 private DWC HAL wrapper and linker symbol wrapping.
- Scoped the P4 TCM heap reservation workaround to ESP-IDF 5.x.
- Added a narrowly scoped LCD initializer compatibility header for the IDF 6
  `in_color_format` field and removed `use_dma2d` flag.
- Added CI jobs that build ESP32-S3 and ESP32-P4 with the ESP-IDF 6.0.2 Docker
  image and run both host regression suites.

## Required validation before merge

- [x] Both IDF 6.0.2 firmware builds pass from clean configuration.
- [x] Existing S3 and P4 host regression tests pass (including PowerShell 5.1 compatibility fixes).
- [ ] P4 display/touch/PSRAM smoke passes.
- [ ] P4 USB MSC cold boot, software reboot, disconnect and reconnect pass without
  the old DWC HAL shim.
- [ ] FLX4 MIDI IN/OUT and UAC isochronous headphone output pass on S3.
- [ ] P4 PCM5102A MAIN output and S3/P4 monitor link pass without underruns.
- [ ] ESP-Hosted AP and AP-to-STA-to-AP OTA round trip pass.
- [ ] Signed OTA verification, rollback and partition-size gates pass on both boards.

The branch must remain a draft until the hardware acceptance rows are complete.
