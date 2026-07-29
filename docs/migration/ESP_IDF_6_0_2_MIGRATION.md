# ESP-IDF 6.0.2 migration

This branch migrates both Pajoniiir firmware targets from ESP-IDF 5.5.4 to
ESP-IDF 6.0.2 while keeping functional behavior unchanged until the dual-target
build and existing host regression suites are green.

## Applied changes

### IDF 6.0.2 platform migration

- Raised both project CMake minimum versions to 3.22.
- Removed the P4 build-time `ESP_IDF_VERSION=5.5` override.
- Pinned both firmware projects to ESP-IDF 6.0.2.
- Added `espressif/usb` 1.5.0 as the S3/P4 USB Host managed dependency.
- Updated the P4 MSC dependency to `espressif/usb_host_msc` 1.2.0.
- Replaced aggregate `driver` dependencies with explicit GPIO/UART/LEDC driver
  components.
- Updated the ST7701 dependency to the IDF-6-compatible 2.0.2 release.
- Removed the IDF 5.5 private DWC HAL wrapper and linker symbol wrapping.
- Scoped the P4 TCM heap reservation workaround to ESP-IDF 5.x.
- Added a narrowly scoped LCD initializer compatibility header for the IDF 6
  `in_color_format` field and removed `use_dma2d` flag.
- Committed both `dependencies.lock` files for reproducible managed-component
  resolution.

### Integrated stability and performance improvements

The following changes were merged from `fix/release-blockers-and-concurrency`
(via `tmp/small-hardening-batch` and `tmp/ordered-cleanup`) and are part of
this branch:

- **Silicon rev 1.x P4 fix**: `sdkconfig.defaults` restores the correct
  pre-v3 silicon revision selector for P4 eco2.
- **Bounded compressed audio cache**: MP3/WAV/FLAC playback uses a seekable
  LRU page cache (8 × 32 KiB per deck) instead of loading the entire
  compressed file into contiguous PSRAM. Eliminates `TRACK TOO LARGE` errors
  and PSRAM fragmentation under large files.
- **Paginated Library UI**: the LVGL library table renders one eight-row page
  with PREV/NEXT navigation buttons (≤40 live LVGL cells) instead of
  materializing up to 5120 cells for a 1024-track catalog.
- **Immutable track sort**: library sorting operates on a double-buffered
  `uint16_t` row-order array over immutable track records. Full-record copies
  and qsort over large structs are eliminated.
- **Single-framebuffer consolidation**: BSP and LVGL backend allocate and
  request only one framebuffer, matching the actual partial-LVGL/PPA rendering
  pipeline and recovering two unused full-screen PSRAM allocations.
- **Lossless control queue**: button/state edge events use backpressure
  instead of fire-and-forget posting; only continuous values (fader, jog) are
  coalesced.
- **ANLZ snapshot ownership**: ANLZ metadata is cloned into task-owned
  snapshots with writer/reader guards, eliminating a borrowed-pointer
  use-after-free.
- **USB reconciliation model**: desired/current state machine with task
  notification, periodic reconciliation and bounded exponential mount retry.
- **WiFi transition lease**: central `wifi_transition_lease` serialises probe
  and pull-OTA transitions so they cannot race the Wi-Fi stack.
- **Recorder safety hardening**: transactional finalisation
  (`patch` → `sync` → `close` → `publish`), producer stop-gate that waits for
  in-flight writers before drain, and propagation of every durability failure.
  The recorder remains compiled out by default.
- **Dead code cleanup**: removed legacy `file_buf`/full-track seek-table
  remnants, retired `audio_output_remaining_delay_ms()` and unused scratch
  buffer APIs, and extracted compressed cache, recorder producer gate and
  finalise transaction into small ownership modules with clean host tests.

### Test and tooling improvements

- Added CI jobs that build ESP32-S3 and ESP32-P4 with the ESP-IDF 6.0.2 Docker
  image and run both host regression suites.
- Fixed PowerShell 5.1 compatibility in `run_p4_host_tests.ps1` (replaced
  `.Split()` with `[regex]::Matches`, normalised CRLF→LF). The migration-era
  `run_p4_host_tests_current.ps1` shim, which rewrote the base runner's source at
  run time, has since been folded back into `run_p4_host_tests.ps1`; there is one
  runner again and it is the file CI executes.
- Added `esp_timer.h` stub for Win32 host test compilation.
- Added new host tests: `audio_compressed_cache`, `audio_recorder_finalize`,
  `audio_recorder_stop_gate`, `ui_library` pagination.
- Updated LVGL UI simulator baselines for the paginated Library table.

## Required validation before merge to master

- [x] Both IDF 6.0.2 firmware builds pass from clean configuration.
- [x] Existing S3 and P4 host regression tests pass (including PowerShell 5.1
  compatibility fixes).
- [x] UI simulator E2E screenshot gate passes with updated baselines.
- [ ] P4 display/touch/PSRAM smoke passes.
- [ ] P4 USB MSC cold boot, software reboot, disconnect and reconnect pass.
- [ ] Sustained USB playback confirms BNA recovery. The DWC channel-decoder wrap
  was briefly dropped on this branch as obsolete; it is not. ESP-IDF 6.0.2 still
  asserts `CHHLTD` for every channel error while documenting `BNAINTR` as the one
  that does not raise it, and `BNAINTR` is in the error mask, so at HAL assertion
  level 2 a BNA panics. It was hit on this board under ESP-IDF 5.5, and the
  mitigation that used to avoid it — preloading each track into PSRAM so playback
  never touched USB — has been replaced by the bounded cache, which streams from
  USB continuously. The wrap is restored and pinned to the IDF version it
  mirrors; `usb_dwc_compat_bna_recovered_count()` makes a recovery observable so
  this row can actually be checked rather than assumed.
- [ ] FLX4 MIDI IN/OUT and UAC isochronous headphone output pass on S3.
- [ ] P4 PCM5102A MAIN output and S3/P4 monitor link pass without underruns.
- [ ] ESP-Hosted AP and AP-to-STA-to-AP OTA round trip pass.
- [ ] Signed OTA verification, rollback and partition-size gates pass on both
  boards.
- [ ] Bounded compressed cache plays real MP3/WAV/FLAC under dual-deck load
  without audible artefacts or cache-miss stalls.
- [ ] Paginated Library table displays, scrolls and loads tracks correctly on
  the P4 touch display.

The branch must remain a draft until the hardware acceptance rows are complete.
