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
- **Decode reads no longer stall the output task**: `AE_LOCK` is one global
  recursive mutex that `ae_output_task` takes for every audio block, so a cache
  miss taken while holding it blocked the priority-6 output task for the whole
  USB transfer — an audible dropout rather than a late decode. The decode loops
  now warm both ends of the next read's page span before taking the lock (the
  cache has a single client, so warming outside the lock races with nothing),
  and `audio_engine_locked_backend_read_count()` counts the reads that still
  land under it, since warming is a prediction that a seek can invalidate.
  Two related items remain open: the lock is still global, so deck 1's decode
  blocks deck 2's, and `library.c` still holds `media_io_gate` across an entire
  PDB parse.
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

## Software gates — complete, and the basis for the merge to master

- [x] Both IDF 6.0.2 firmware builds pass from clean configuration.
- [x] Existing S3 and P4 host regression tests pass (including PowerShell 5.1
  compatibility fixes).
- [x] UI simulator E2E screenshot gate passes with updated baselines.

## Hardware acceptance — in progress on master

The migration was merged with these rows open because hardware was not then
available. Hardware work started on 2026-08-02, but the migration is still
**not release-qualified**. Nothing below is inferred from a green build — a
successful CI run says the code compiles and the host models agree, not that
the silicon behaves. Work the remaining rows in order.

- [x] P4 full wired flash boots an ESP-IDF v6.0.2 bootloader and
  `RC2-3-g136aad7`; ESP-Hosted slot 1 and microSD slot 0 share the single SDMMC
  controller, and the repaired 59,688 MB exFAT SDHC card mounts in 4-bit mode.
  See `../validation/P4_IDF6_SDMMC_SMOKE_20260802.md`. This focused pass does
  not close display, USB, audio, OTA or sustained-media rows below.
- [x] S3 full wired flash writes and verifies the clean RC2 ESP-IDF v6.0.2
  bootloader/application pair. The running application reports `RC2`, `ota_0`,
  `VALID` over the P4 control link. See
  `../validation/S3_IDF6_WIRED_FLASH_20260802.md`. This boot-chain pass does not
  close the FLX4 MIDI/UAC row below.

- [x] P4 display/touch/PSRAM, Settings and paginated Library focused smoke
  passes. Operator-confirmed 2026-08-02; see
  `../validation/RC2_FOCUSED_FUNCTIONAL_SMOKE_20260802.md`.
- [ ] P4 USB MSC cold boot, software reboot, disconnect and reconnect pass.
  Focused Library/MP3 access passed, but the complete recovery sequence did not.
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
- [x] FLX4 MIDI/LED and UAC isochronous headphone output pass in the focused
  S3 smoke. Numeric sustained-link soak remains covered by the counter row.
- [x] P4 PCM5102A MAIN output and the S3/P4 monitor path are audible and
  functional in the focused smoke. Long-duration underrun/counter evidence
  remains pending.
- [ ] ESP-Hosted AP and AP-to-STA-to-AP OTA round trip pass. The core pull-OTA
  path passed before migration; the hardened path still needs an IDF6 re-smoke.
- [ ] Signed OTA verification, rollback and partition-size gates pass on both
  boards. Positive signed RC2 OTA succeeded on both targets and clean-build
  size evidence passes; migrated negative-path and rollback re-smoke remains.
- [ ] Bounded compressed cache plays real MP3/WAV/FLAC under dual-deck load
  without audible artefacts or cache-miss stalls. Focused real-MP3 playback
  passed 2026-08-02, but the audited USB had 68 MP3 files and no physical
  WAV/FLAC files; two selected WAV PDB rows referenced missing files, so those
  formats and the sustained dual-deck load remain untested.
- [x] Paginated Library table displays, navigates and participates in the
  focused load check on the P4 touch display.
- [ ] `audio_engine_locked_backend_read_count()` stays flat under sustained
  dual-deck playback. The decode loops warm the compressed cache before taking
  `AE_LOCK` so a USB read cannot stall the output task, but warming is a
  prediction and a seek can invalidate it. A rising count means reads are
  landing back under the lock, which is heard as a dropout rather than seen in
  any log.
