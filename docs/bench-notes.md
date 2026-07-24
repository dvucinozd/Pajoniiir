# Bench Notes

Document status: dated hardware evidence, reviewed 2026-07-16. New observations
should include date, firmware version, board, port and pass/fail evidence.

## Test Setup

| Item | Value |
| --- | --- |
| Date | 2026-05-21 |
| ESP32-S3 board | ESP32-S3-DevKitC-1 N16R8 |
| ESP32-P4 board | JC4880P443C_I_W — RECEIVED & VERIFIED ✅ |
| ESP-IDF version | v5.5 (Custom framework path) |
| IDF path | `C:\Espressif\frameworks\esp-idf-v5.5\` |
| Python venv | `C:\Espressif\python_env\idf5.5_py3.11_env` |
| S3 flash port | COM4 (CH343 UART bridge, GPIO43/44) |
| P4 flash port | COM15 (CH343 UART bridge, GPIO43/44) ✅ |
| S3 MIDI port | COM5 → USB-OTG (GPIO19/20), becomes VID_303A:PID_4008 after firmware |
| Notes | P4 hardware received, verified, stabilized under heavy USB loads |

Known toolchain warning: `esp_codec_dev` Kconfig may warn that `ESP_IDF_VERSION`
environment variable is not set when building through the manual Windows IDF
environment. The firmware build completes; keep using the documented IDF 5.5
Python/toolchain paths until the local Espressif export environment is repaired.

---

## ESP32-S3 Smoke Tests

| Test | Firmware | Expected | Result | Notes |
| --- | --- | --- | --- | --- |
| Flash via COM4 | V1 firmware | esptool connects, writes, hard reset | **PASS** | 460800 baud, ~4s |
| Boot log on COM4 | V1 firmware | All subsystem init messages | **PASS** | All 6 subsystems log OK |
| USB MIDI enumerate | V1 firmware | VID_303A:PID_4008 appears on USB | **PASS** | Requires replug of USB-OTG cable after first flash |
| 13 buttons init | V1 firmware | `panel_io: ready, 13 buttons` | **PASS** | Confirmed in boot log |
| Jog encoder init | V1 firmware | `encoder: jog A=15 B=16` | **PASS** | PCNT unit starts |
| Pitch ADC init | V1 firmware | `pitch: ADC1 ch0 (GPIO1) ready` | **PASS** | |
| 4 LEDs init | V1 firmware | included in panel_io ready log | **PASS** | |
| TinyUSB MIDI driver | V1 firmware | `TinyUSB Driver installed on port 0` | **PASS** | |
| UART1 control link | V1 firmware | `ctrl_link: UART1 TX=40 RX=41` | **PASS** | Communicates flawlessly with P4 |
| USB-Serial/JTAG conflict | V1 firmware | COM5 does NOT appear as JTAG after boot | **PASS** | `CONFIG_ESP_CONSOLE_SECONDARY_NONE=y` |

### Key S3 sdkconfig findings

| Setting | Value | Why critical |
| --- | --- | --- |
| `CONFIG_TINYUSB_MIDI_COUNT` | `1` | Default is 0; causes linker error if missing |
| `CONFIG_ESP_CONSOLE_SECONDARY_NONE` | `y` | USB-JTAG secondary conflicts with TinyUSB on GPIO19/20 |
| `CONFIG_TINYUSB_MODE_SLAVE` | `y` | DMA mode caused enumeration failure on this hardware |
| `CONFIG_FREERTOS_HZ` | `1000` | Required for 1ms debounce timer resolution |

### Confirmed ESP32-S3 Pin Assignments

| Function | GPIO | Confirmed |
| --- | --- | --- |
| Pitch ADC (ADC1 CH0) | GPIO1 | Yes |
| BTN_EJECT | GPIO2 | Yes |
| BTN_TRACK_PREV | GPIO3 | Yes |
| BTN_TRACK_NEXT | GPIO4 | Yes |
| BTN_SEARCH_BACK | GPIO5 | Yes |
| BTN_SEARCH_FWD | GPIO6 | Yes |
| BTN_CUE | GPIO7 | Yes |
| BTN_PLAY | GPIO8 | Yes |
| BTN_PERF1 (Jet) | GPIO9 | Yes |
| BTN_PERF2 (Zip) | GPIO10 | Yes |
| BTN_PERF3 (Wah) | GPIO11 | Yes |
| BTN_HOLD | GPIO12 | Yes |
| BTN_MODE | GPIO13 | Yes |
| BTN_MASTER_TEMPO | GPIO14 | Yes |
| JOG_A | GPIO15 | Yes |
| JOG_B | GPIO16 | Yes |
| LED_CUE | GPIO33 | Yes |
| LED_PLAY | GPIO34 | Yes |
| LED_BEAT | GPIO38 | Yes |
| LED_END | GPIO39 | Yes |
| UART1 TX → P4 | GPIO40 | Yes |
| UART1 RX ← P4 | GPIO41 | Yes |
| USB D− (TinyUSB) | GPIO19 | Hardware fixed |
| USB D+ (TinyUSB) | GPIO20 | Hardware fixed |
| UART0 TX (console) | GPIO43 | Hardware fixed (CH343) |
| UART0 RX (console) | GPIO44 | Hardware fixed (CH343) |

---

## ESP32-P4 / JC4880P443C_I_W Smoke Tests

| Test | Firmware/example | Expected | Result | Notes |
| --- | --- | --- | --- | --- |
| Flash via COM15 | V1 P4 Firmware | esptool connects and flashes | **PASS** | Flashing stable over UART |
| Boot log on COM15 | V1 P4 Firmware | Subsystem initialization logs | **PASS** | MIPI DSI, PSRAM, USB, UI boot OK |
| LCD Init (ST7701S) | V1 P4 Firmware | Displays LVGL DJ UI layout | **PASS** | ST7701S panel up (480x800, DPI) |
| LCD Backlight | V1 P4 Firmware | PWM backlight control working | **PASS** | Toggles and dims on GPIO23 |
| Touch (GT911) | V1 P4 Firmware | Responsive touch screen | **PASS** | GT911 registered on I2C SDA=7 SCL=8 |
| USB Host Mount | V1 P4 Firmware | USB Drive mounts to `/usb` | **PASS** | Fast USB Host MSC mounting at boot |
| Media Library Load | V1 P4 Firmware | 308 tracks parsed and indexed | **PASS** | **308 Rekordbox tracks indexed in 261ms** |
| SDMMC TF card mount | V1 P4 Firmware | TF card mounts to `/sd` | **PASS** | JC4880 SD pins are on P4 SDMMC slot 0; verified with SA32G 32 GB SDHC, FAT32, 4-bit bus |
| P4 ↔ S3 UART Link | V1 P4 Firmware | Handles commands from S3 | **PASS** | Fixed on GPIO28 RX and GPIO29 TX |
| Audio playback (ES8311) | V1 P4 Firmware | MP3 from `/usb` plays via I2S | **PASS** | minimp3 → ES8311; PSRAM preload; 44.1/48 kHz; stable across many loads |
| Touch control path | V1 P4 Firmware | PLAY/PAUSE, hot cues, beat jump drive audio | **PASS** | via `deck_core` → `audio_engine`; header/waveform track live position |
| Loop playback | V1 P4 Firmware | Loop in/out repeats gaplessly | **PASS** | after gapless fix (loop wrap no longer flushes ring); short ~1-beat loops OK |
| On-screen beat indicator | V1 P4 Firmware | 4-beat pulse indicator updates from beatgrid/BPM | **PASS** | Build + boot verified; uses PQTZ beatgrid with BPM fallback |

### Hosted Wi-Fi / Web UI Bench Plan

| Test | Firmware/example | Expected | Result | Notes |
| --- | --- | --- | --- | --- |
| ESP32-C6 hosted Wi-Fi firmware | Current P4 build | P4 initializes hosted Wi-Fi only when Settings Wi-Fi Remote is ON | **PASS** | Re-enabled 2026-07-04 behind `app_settings.wifi_remote`; default off keeps RF quiet |
| Web UI SoftAP | Current P4 startup | SoftAP starts after user enables Wi-Fi Remote | **PASS** | SoftAP `PAJONIIR` starts on `192.168.4.1` after hosted/AP init succeeds |
| Captive portal HTTP | Phone/PC client | `/`, `/api/status`, `/api/library`, `/api/load` respond on AP IP | **PASS** | Mobile controller reachable at `http://192.168.4.1` when Wi-Fi Remote is enabled |
| Captive DNS | Phone/PC client | arbitrary DNS queries resolve to the P4 AP IP without malformed-packet crash | **PASS** | Captive DNS starts only after the Wi-Fi/AP stack is initialized |
| Concurrent web load | Browser double-click/load spam | Second `/api/load` is rejected while load worker is busy | **PENDING** | Prevents concurrent P4 track load workers |

### Key P4 PSRAM & Performance Optimizations

To prevent core panic, memory exhaustion, and watchdog resets when loading large media libraries (300+ tracks), the following stability configurations were successfully implemented:

1. **Pointer-Based Library Engine**:
   - Replaced heavy `library_get(index, out_struct)` (which copied 2.9 KB track chunks to the calling task's stack) with a direct pointer interface: `library_get_ptr(index)`.
   - Reduced stack strain and O(n) overhead inside search/render loops.

2. **PSRAM Cache Execution**:
   - Instruction and read-only data are cached directly from PSRAM to maximize execution speed:
     ```ini
     CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y
     CONFIG_SPIRAM_RODATA=y
     CONFIG_SPIRAM_XIP_FROM_PSRAM=y
     CONFIG_SPIRAM_FLASH_LOAD_TO_PSRAM=y
     ```

3. **Memory Mapped Allocations**:
   - Configured the system to prioritize SPIRAM (PSRAM) for all allocations larger than 256 bytes, saving internal SRAM for low-latency kernel queues and stack data:
     ```ini
     CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=256
     ```
   - Enabled standard C library malloc/realloc for LVGL, forcing complex cells and large widgets to allocate from 32MB PSRAM instead of scarce 64KB internal SRAM pools:
     ```ini
     CONFIG_LV_USE_CLIB_MALLOC=y
     ```

4. **Results**:
   - Table loading speed for 308 tracks: **261 ms** (previously locked up or crashed).
   - Internal SRAM remaining after loading: **>400 KB** (previously depleted to 0 KB).
   - Free PSRAM remaining: **27.3 MB**.
   - Fully responsive touch inputs and GUI without any frozen states or watchdog timeouts.

### Confirmed P4 Pin Assignments

| Function | GPIO | Source | Verified |
| --- | --- | --- | --- |
| LCD backlight | GPIO23 | Board Schema | **Yes** |
| LCD reset | GPIO5 | Board Schema | **Yes** |
| Touch I2C SDA | GPIO7 | Board Schema | **Yes** |
| Touch I2C SCL | GPIO8 | Board Schema | **Yes** |
| Codec I2C (shared) | GPIO7/8 | Board Schema | **Yes** |
| I2S MCLK | GPIO13 | Board Schema | **Yes** |
| I2S BCLK | GPIO12 | Board Schema | **Yes** |
| I2S LRCK | GPIO10 | Board Schema | **Yes** |
| I2S DIN | GPIO48 | Board Schema | **Yes** |
| I2S DOUT | GPIO9 | Board Schema | **Yes** |
| Speaker PA | GPIO11 | Board Schema | **Yes** |
| SDMMC D0-D3 | GPIO39-42 | Vendor demo + hardware smoke test | **Yes** |
| SDMMC CMD/CLK | GPIO44/43 | Vendor demo + hardware smoke test | **Yes** |
| UART1 RX ← S3 | GPIO28 (JP1 pin 19) | Custom Pinout | **Yes** |
| UART1 TX → S3 | GPIO29 (JP1 pin 12) | Custom Pinout | **Yes** |

---

## Audio Notes

| Check | Result | Notes |
| --- | --- | --- |
| MP3 decode + ES8311 output | **PASS** | minimp3 → `esp_codec_dev`/I2S; plays from `/usb`; 44.1 & 48 kHz |
| Dual-deck audio scheduling | **PASS** | 2026-06-20 P4 run: both decks playing with normal audio and waveform after active-output preload chunks were reduced to 32 KB, seek-table publication was moved to a short lock, codec write pacing was allowed to own timing, and preload diagnostics were throttled |
| Audio output diagnostics | **PASS** | 2026-06-21 P4 run: `diag output late` warning spam removed by using a precise µs block period and a 2x-period outlier threshold. Dual-deck smoke reported `DIAG_OUTPUT_LATE_COUNT=0`, healthy rings, and stable decode timing. |
| Stability under load | **PASS** | MUST preload MP3 to PSRAM + decode via `fmemopen`; streaming from USB during playback trips a USB-DWC channel assert (`usb_dwc_hal.c:502`) → reboot |
| Decode task stack | Note | minimp3 needs ~26 KB → dedicated 32 KB decode task |
| Output routing topology | **PASS** | Current product path is PCM5102A RCA MAIN OUT plus FLX4 USB headphones. The retired Settings speaker/monitor switch was removed from active UI during the 2026-07-08 polish pass. |
| Settings persistence (NVS) | **PASS** | `app_settings` stores master trim, backlight, time mode, and Wi-Fi Remote state; power-cycle persistence verified across Settings work |
| PWM backlight | **PASS** | LEDC 10-bit @ 5 kHz on GPIO23; SETTINGS slider smoothly dims/brightens the panel |
| Line-level output viable | **PASS** | PCM5102A MAIN OUT acceptance passed 2026-06-30 through RCA and onboard 3.5 mm output; ES8311 RCA tap is no longer the product path |

---

## ESP-Hosted / Wi-Fi Bring-Up

| Check | Result | Notes |
| --- | --- | --- |
| P4 flash target | **PASS** | `COM15`, ESP32-P4 rev v1.3, MAC `80:f1:b2:d0:b4:9b` |
| ESP-Hosted SDIO pins | **PASS** | Log confirms slot 1, 4-bit, CLK 18, CMD 19, D0-D3 14-17, C6 reset 54 |
| Host transport init | **PASS** | C6 identified as `esp32c6`; SDIO card init successful; transport active |
| SoftAP + web UI | **PASS** | Re-enabled 2026-07-04 behind the Settings switch; SoftAP `PAJONIIR` on `192.168.4.1`, mobile web controller reachable |
| ESP-Hosted mempool | **FIXED** | Initial HOST boot asserted in `sdio_mempool_create` because SDIO mempool allocated ~48 KB internal DMA RAM. Enabling `CONFIG_ESP_HOSTED_MEMPOOL_PREFER_SPIRAM=y` fixes boot on this P4 workload. |
| C6 firmware version | **FIXED** | Upgraded onboard C6 over `COM12` to ESP-Hosted slave `2.12.8` using USB-TTL on `PROG_C6`. Boot log now identifies `esp32c6`, reports `Transport active`, and no longer prints the Host/Co-proc version mismatch warning. |
| C6 slave firmware build | **PASS** | Built and flashed `firmware/main-deck-p4/managed_components/espressif__esp_hosted/slave/build/network_adapter.bin`; stable flashing required P4 held in bootloader and C6 flashing at 115200 baud because 460800 stopped responding through the jumper wiring. |
| SD cache mount | **FIXED** | `/sd` now mounts on boot with the inserted `SA32G` 29.5 GB card: 4-bit SDMMC, 20 MHz. Root cause was missing vendor SD power control: on-chip LDO channel 4 must be attached to `host.pwr_ctrl_handle` before `esp_vfs_fat_sdmmc_mount()`. |

## 2026-07-16 Signed OTA Deployment

This entry records OTA delivery and boot/version observations only. No complete
functional audio/UI/controller smoke was run after this rollout.

| Target board | Transport/endpoint | Before | After | Upload | Result |
| --- | --- | --- | --- | --- | --- |
| JC4880P443C_I_W ESP32-P4 | P4 Wi-Fi Remote, `POST /api/ota/p4` | `ota_0 / RC1-126-g812ad70f` | `ota_1 / RC1-131-gc391e306`; healthy `/api/status` | HTTP 200 | **DEPLOYMENT PASS** |
| Seeed XIAO ESP32S3 | P4 Wi-Fi Remote forwarding, `POST /api/ota/s3` | `ota_1 / RC1-123-g587cd7a1` | `ota_0 / valid / RC1-131-gc391e306`, confirmed through P4 | HTTP 200 | **DEPLOYMENT PASS** |

Both `rel-001` bundles and the outer manifest verified before upload. P4's
top-level firmware `state=idle` after reboot is the local transfer state, not an
image-validity result; the nested S3 report supplied S3's image state. Overall
result: **SIGNED DEPLOYMENT/BOOT PASS; FUNCTIONAL HARDWARE SMOKE NOT RUN**. The
latest fully functionally accepted release remains `RC1-123-g587cd7a1`.
Artifact hashes, sizes and state transitions are in
[`validation/SIGNED_OTA_RC1_131_DEPLOYMENT.md`](validation/SIGNED_OTA_RC1_131_DEPLOYMENT.md).

### Matching `RC1-168-gb69f1b19` rollout, 2026-07-21

| Target board | Transport/endpoint | Before | After | Upload | Result |
| --- | --- | --- | --- | --- | --- |
| Seeed XIAO ESP32S3 | S3 Debug AP, `POST /api/ota/s3` | `ota_1 / RC1-146-g75feb6f1` | `ota_0 / valid / RC1-168-gb69f1b19` | HTTP 200, 9.1 s | **DEPLOYMENT PASS** |
| JC4880P443C_I_W ESP32-P4 | P4 Wi-Fi Remote, `POST /api/ota/p4` | `factory / RC1-166-g4bda7976` | `ota_0 / RC1-168-gb69f1b19` | connection closed at reboot, transfer completed | **DEPLOYMENT PASS** |

Both boards now run the same signed release from `ota_0`, confirmed directly and
through P4's nested S3 report. The P4 upload returned no HTTP status because the
board rebooted before answering — always confirm an OTA through `/api/firmware`
rather than trusting the curl exit status.

Two hardware-found defects were fixed on the way to this build: the USB preload
held `media_io_gate` for the whole file (a cached metadata load measured 2367 ms
under a concurrent preload versus 47 ms idle; now released per 32 KB chunk,
measured 267 ms), and adding three recorder endpoints exceeded
`max_uri_handlers`, which made `register_uri_or_stop()` stop the entire web
server — the SoftAP stayed up while every endpoint including OTA was dead, and
recovery required a wired COM15 flash.

Accepted on hardware in this build: the master-output recorder functional `.wav`
capture (about 54 s, 0 dropped blocks/frames, 4.7 % ring high-water, exact
176.4 kB/s) and the structured service journal. Not run: the full functional
audio/UI/controller smoke, so the latest fully accepted release remains
`RC1-123-g587cd7a1`.

---

## Open Items
- ⚙️ **USB filesystem/layout support:** P4 firmware now has a planned closure for
  FAT32/exFAT across superfloppy, MBR, and GPT layouts in
  `docs/superpowers/specs/2026-07-03-p4-usb-exfat-gpt-design.md`. Hardware
  acceptance is tracked in `docs/validation/P4_USB_EXFAT_GPT_SMOKE.md`.
- ✅ **Control path verified on hardware (touch):** PLAY/PAUSE, CUE, hot cues, beat jump, loop, and
  live header/waveform tracking all work via `deck_core` → `audio_engine`. CUE is now an on-screen
  button (OVERVIEW, right of the upper waveform); the physical S3 CUE will reuse the same path.
  CUE returns to the cue point (track start by default) and pauses, in any play state.
- ✅ **Tap-to-seek (needle drop):** tapping the **upper** overview waveform jumps playback to the
  absolute position (full-track coarse seek); tapping the **lower** high-res zoom waveform seeks
  relative to the centre needle (fine seek, ZOOM_BAR_MS per ZOOM_BAR_PITCH_PX). Both mirror the
  playhead mapping and preserve play/pause state. Verified on hardware.
- ✅ **Seek crash/freeze fixed (no-PVBR tracks):** tapping the waveform to seek far ahead on a
  track whose PVBR table is all zeros used `seek_linear`, which decoded the MP3 from the file start
  to the target in a tight non-yielding loop → starved CPU 0 (task WDT every 5 s, `ae_decode`) and
  froze the UI; it could spin forever if the target was beyond the bytes streamed from USB
  (~0.4 MB/s). Replaced with `seek_estimate` — O(1) byte interpolation (~CBR) + minimp3 resync.
  Seeking past the loaded region is handled by the decode loop's load gate (waits with vTaskDelay).
  All seek paths are now O(1) (IFI index / PVBR / estimate). Verified on hardware (no WDT).
- ✅ **Overview readability:** upper waveform now dims the played portion (bright→dim edge = current
  position, easy to follow) and the playhead is a brighter 3 px neon-red line.
- ⚠️ **Seek noise (open):** a brief artefact can be heard right after a seek — the MP3 decoder is
  reset (`mp3dec_init`) so the bit reservoir / synthesis filterbank are empty and the first frame
  is garbage. An attempt to decode-and-discard the first 2 frames after each seek caused audible
  **crackling during normal playback** and was **reverted** (commit isolation confirmed it as the
  cause). Revisit with a gentler approach (e.g. a short PCM fade-in on the first post-seek frames,
  or zeroing only the corrupt samples) rather than dropping whole frames.
- ✅ Load-to-play latency (P5): "LOADING NN%" indicator + progressive preload (loader task +
  gated decoder) → playback starts ~0.3 s instead of 1–6 s. USB read measured ~1 MB/s; hidden
  by streaming the rest in the background. Verified on hardware (clean audio, no underrun).
- ✅ **Dual-deck audio scheduling fix (2026-06-20):** when Deck 2 was started
  while Deck 1 was already playing, audio could slow/pop and the waveform
  could become non-fluid. COM15 diagnostics showed full PCM rings and enough
  heap/PSRAM, so the root cause was scheduling/pacing rather than decoder
  underflow. The fix reduced active-output preload chunks from 256 KB to 32 KB,
  built MP3 seek tables outside the long engine lock before a short publish,
  removed the extra output-task delay after `esp_codec_dev_write()`, and
  changed aggressive preload logging to periodic summaries. Hardware retest
  confirmed normal audio and waveform with both decks playing.
- ✅ **S3 MIDI host responsiveness fix (2026-06-21):** when both decks were
  playing, controller input could stop responding until the S3 was restarted.
  Logs showed USB MIDI transfer errors plus MIDI OUT queue pressure. Raw USB
  MIDI packet logs are now DEBUG-only in translator mode, and FLX4 VU meter
  packets are dropped when the USB MIDI OUT queue has backlog. Hardware retest
  confirmed Play/Pause remains responsive with both decks running.
- ✅ **P4 audio diagnostic spam fix (2026-06-21):** normal blocking
  `esp_codec_dev_write()` pacing was previously compared against a rounded
  256-frame period and emitted continuous `diag output late` warnings even
  with healthy rings. The warning now uses a precise microsecond period and a
  2x-period outlier threshold. Hardware retest with both decks playing reported
  zero late warnings while aggregate output telemetry remained available.
- ✅ `bsp_sd_init()` SDMMC (config/cache): `/sd` mounts on the JC4880 TF slot. Root cause of the
  earlier timeout was `SDMMC_HOST_DEFAULT()` selecting slot 1 while this board is wired to slot 0.
- ✅ Line-level output: PCM5102A MAIN OUT RCA and onboard 3.5 mm output verified
  on 2026-06-30; ES8311 DAC-to-RCA is no longer the product path.
- ✅ Display tearing fix — DPI triple buffering wired into the flush (PPA → non-displayed fb,
  then draw_bitmap flips at frame boundary). Verified on hardware: no tearing.
- ✅ UI polish — visual/interaction refinement across Settings and Overview is
  tracked in Phase 10 of `docs/DEVELOPMENT_PLAN.md`.
  - ✅ English labels + universal dim-on-press feedback; chrome palette centralised in `ui_theme.h`.
  - ✅ Header BPM/pitch were blank — **LVGL builtin `vsnprintf` has no `%f`** unless `LV_USE_FLOAT`
    is on (which also flips `lv_value_precise_t` to float — unwanted). Fix: format decimals via
    integer math (`%d.%02d`), never `%f`, in any `lv_label_set_text_fmt`.
  - ✅ Header: BPM/pitch right-aligned to the edge; added a **remaining-time** counter beside the
    elapsed one (`-MM:SS.cc`), yellow ≤30 s / red ≤10 s to track end. Old click-toggle removed.
- ⚠️ **Overview waveform fluidity bench (2026-06-12, historical measurement):** dual-deck main
  waveforms were moving on hardware, but Deck 2 lower waveform still visibly jittered. Instrumented
  COM15 monitor run while both decks were playing showed:
  - D1 main renderer: avg ~1.7 ms, max ~5.7 ms; D2 main renderer: avg ~2.2 ms, max ~5.6 ms.
  - D1 overlay total: avg ~7.2-7.9 ms, max up to ~20.2 ms; D2 overlay total: avg ~7.5-8.3 ms,
    max up to ~16.5 ms.
  - Overlay cost is dominated by I8→RGB565 conversion (~4-5 ms per deck), then PPA copy/rotate
    (~2.4-2.6 ms per deck). `esp_cache_msync` averages below 1 ms but can spike.
  - `ui_update` still averages ~15-17 ms with ~37-41 ms spikes, and LVGL handler/full-frame flush
    can spike near 190-206 ms.
  - Conclusion at the time: the next optimization candidate was removing I8→RGB565 conversion
    from the hot path or narrowing the live zoom surface to one active deck.
- ✅ **Deck 2 lower Overview waveform jitter fix (2026-06-13):** user-visible jitter on Deck 2 was
  eliminated by keeping Deck 2 on the normal LVGL invalidate/flush path and allowing direct PPA
  overlay only for Deck 1. The scheduler still has an adaptive two-deck redraw budget when both
  decks are playing. Verified with host UI tests, `idf.py build` for `firmware/main-deck-p4`,
  COM15 flash/smoke capture (`bad_lines=0`), and hardware visual confirmation.
- **Deferred to S3/chassis phase:** physical panel controls → `deck_core` queue; Beat LED feedback
  (PQTZ → S3 LED); wire the front panel to the S3 per `PINOUT.md`; mount display in the chassis opening.

## Recorder real-time bench (2026-07-21, `RC1-170` / `RC1-171`)

Instrumented `audio_recorder_push_master()` with a wall-clock bracket
(`push_count`, `push_max_us`, `push_over_100us`, exposed on `GET /api/recording`
and written into the journal by `RECORDING_STOPPED`) to settle the "p99 under
100 us per block" gate.

**Producer copy cost — passes.** 120 s of dual-deck playback while recording
(48 kHz, boot 23):

| measure | result |
|---|---|
| pushes >= 100 us | 9 of 22 593 = **0.04 %** |
| dropped blocks / frames | **0 / 0** |
| ring high-water | 172 / 508 slots (34 %) |
| throughput | 192 281 B/s vs 192 000 B/s theoretical |
| byte/frame agreement | 14 744 064 x 4 = 58 976 256, exact |

**But end-to-end output timing degrades while recording — open item.** The same
boot logged ten `AUDIO_OUTPUT_LATE` warnings between ms 205 417 and 340 417,
with the worst-case late time growing to **370 305 us**. The cluster falls
entirely inside the recording window and stops when recording stops. A later
single-deck session (boot 24) logged `AUDIO_UNDERRUN` during recording and a
single **167 662 us** push.

Caveat on the metric: the bracket measures wall-clock across the copy, so it
cannot separate "the copy was slow" from "the audio output task was preempted
mid-copy". The 167 ms sample almost certainly reflects preemption, since it
coincides with an underrun rather than with any change in copy size. The
aggregate distribution is still the useful signal; the single max is not.

Conclusion: the producer boundary is cheap enough, but recording perturbs output
scheduling. Do not treat the recorder as timing-neutral until the
`AUDIO_OUTPUT_LATE` cluster is explained.

Related: an earlier attempt on this bench recorded `reset=PANIC` on `RC1-170`
(boot 23) and two unexplained `POWERON` resets (boots 20 and 22), one of which
killed an OTA upload mid-transfer. Cause not established; coredump cannot be
enabled on this board (it boot-loops before `app_main`).

## Web layer: socket exhaustion looks exactly like a dead board (2026-07-21)

While verifying the redesigned web controller, `curl` from a second machine got
`HTTP=000` on every endpoint while the operator's phone was rendering and
driving the same page normally. ICMP answered in 2-6 ms and the SoftAP was
healthy, so the board looked alive and the web layer looked dead.

It was neither. `httpd` ran with `max_open_sockets = 5` and
`lru_purge_enable` unset (defaults false), so once five keep-alive sockets were
held the server **refused** further connections instead of evicting the oldest.
The controller page holds sockets while polling `/api/status` every 250 ms, so
one busy browser could occupy the pool and lock every other client out —
including `/api/ota/p4`, which is the only remote recovery path.

Confirmed by closing the browser: access returned on its own after roughly 20 s,
as the idle sockets timed out. Fixed by setting `lru_purge_enable = true`
(`RC1-175-ge40b7225`) and verified on hardware — 8 concurrent requests against
the 5-socket server all returned 200, plus a follow-up request.

Diagnostic value: "pings fine, one client works, every new client gets nothing"
means socket exhaustion, not a crash. Do not reach for a wired COM15 recovery on
that signature alone. Compare with the separate `max_uri_handlers` trap, where a
single failed registration stops the entire server at boot.

## Attributing AUDIO_OUTPUT_LATE (2026-07-22, `RC1-178-g8c689d27`)

The 2026-07-21 entry above concluded the recorder was "not timing-neutral" from
a correlation: `AUDIO_OUTPUT_LATE` clustered inside the recording window. That
correlation was confounded — the web controller page was open and polling
throughout the same window. This entry supersedes that conclusion.

`RC1-178` times each phase of one output block (snapshot prep, the 256-frame
mixer loop, the recorder tap, the monitor link write, the blocking PCM5102A
write, the codec write, AE_LOCK bookkeeping), exposes the maxima in
`/api/status` diagnostics, and zeroes them when a recording starts.

Measured on hardware, two decks playing, 44.1 kHz, threshold 11 610 us:

| condition | late blocks | worst block |
|---|---|---|
| playback only, 90 s | **0** | — |
| playback + recording, sparse polling, 90 s | 1 | 11 919 us (2.6 % over) |
| playback + recording, shipped UI poll rate (~2.5 req/s), 60 s | **0** | — |
| playback + recording, forced ~9 req/s, 60 s | **50** | 20 248 us |
| playback + forced ~9 req/s, **no recording**, 642 requests | **0** | — |

Neither recording nor web traffic alone produces late blocks; only the two
together, and only well above the rate the shipped page actually polls at.

The mechanism is preemption, not slow code. Under the forced load **every phase
inflated proportionally, including phases with no extra work to do** — the codec
write went 57 -> 1539 us and the pure struct-prep head phase went 160 -> 3002 us.
Nothing started doing more; the output task was being descheduled at arbitrary
points. A plausible shared resource is the PSRAM bus: the recorder ring,
decoded PCM, the DSI framebuffer and the ESP-Hosted C6 mempool (which
deliberately prefers SPIRAM) all sit on it.

What recording actually costs: roughly 15-20 % of the headroom in every phase
(mixer 6633 -> 7829 us, bookkeeping 1847 -> 2384 us) plus the 512 KiB PSRAM
ring. That is a narrower margin, not a defect at the operating point.

**Residual, deliberately left open:** the 370 305 us worst case recorded on
2026-07-21 did **not** reproduce. The worst this bench could force was 20 ms,
and only under deliberate overload. An outlier an order of magnitude larger
remains unexplained, on a bench that also logged an unexplained `reset=PANIC`
the same day. Do not treat the 370 ms event as covered by this analysis.

## USB did not re-enumerate after a software reset (2026-07-22)

The track library came up empty after every OTA update until the stick was
physically unplugged and replugged. The service journal separates the two reset
paths cleanly:

| boot | reset | firmware | USB_MOUNTED |
|---|---|---|---|
| 33 | POWERON | RC1-175 | 1 389 ms |
| 35 | POWERON | RC1-178 | 1 437 ms |
| 36 | SW | RC1-180 | 260 394 ms — only after a manual replug |
| 37 | SW | RC1-181 | 138 939 ms — only after a manual replug |
| 38 | SW | RC1-182 (fix) | **7 005 ms, unattended** |
| 39 | SW | RC1-183 | 7 846 ms |
| 40 | SW | RC1-184 (tuned) | **4 095 ms** |

`reset=POWERON` mounted in about 1.4 s every time; `reset=SW` never mounted on
its own, and silently — no `USB_MOUNT_FAILED`, the host simply never saw a
device. After `esp_restart()` the drive is still powered and configured from the
previous session, so the newly installed host stack waits for a connection event
that cannot occur, because nothing about the connection changed.

Fix: install the host with `root_port_unpowered` and then power the root port on
with the public `usb_host_lib_set_root_port_power()`, reproducing the power-on
sequence deliberately (rather than the deprecated `usb_phy_action()` disconnect).
The lib task also stopped blocking forever on `usb_host_lib_handle_events()` —
with no drive attached there are no events at all — and re-cycles the port while
nothing has ever connected.

**What the measurements corrected:** the first power-on does *not* re-enumerate
an already-attached drive, and widening the port-off window does not help
(120 ms → 7 005 ms, 400 ms → 7 846 ms, both mounted by the retry rather than the
initial cycle). The likely reason is that the root port's logical power state
does not cut VBUS on this board, so the drive never loses power and never resets
itself. Repeating the cycle is the mechanism that works, so the off window was
put back to 150 ms and the early retry shortened to 900 ms, which mounts at
4 095 ms. The residual gap against a 1.4 s cold boot is the cost of those cycles
and was accepted.

## The recorder's real problem is the microSD card (2026-07-22, later)

This supersedes the entry above it. That entry concluded, from a 7-minute run,
that trimming journal writes had fixed the recorder's audio stalls (224 ms ->
18.7 ms, "12x"). Two 25-minute soaks show that conclusion was drawn from a
window too short to contain the failure, which arrives roughly once per two
minutes.

Soaks ran two decks continuously, reloading a deck as soon as it ran out so the
run measured real audio rather than paced silence, with recording active
throughout.

| | 7 min (the claim) | soak 1, 25 min | soak 2, 25 min |
|---|---|---|---|
| worst block | 18 733 us | 320 617 us | 356 106 us |
| late blocks | 7 | 66 | 293 |
| recordings lost | — | 2 | 0 |

### What the card actually does

`RC1-191` times every block write. The answer was unambiguous — a single 1 KiB
block write blocked for **553 ms**, against an expected sub-10 ms, and the
journal caught the shape of it:

```
ms=523612  stall 362 ms   ring  80/508
ms=523972  stall 359 ms   ring 147
ms=524333  stall 361 ms   ring 213
ms=524591  stall 257 ms   ring 260
ms=525014  stall 366 ms   ring 334
ms=525375  stall 360 ms   ring 400
ms=525738  stall 362 ms   ring 468
ms=525989  stall 251 ms   ring 508  -> dropped
```

The card periodically enters a state where **every** write costs ~360 ms, back
to back. Eight consecutive stalls is ~2.9 s, and the 512 KiB ring holds ~2.95 s,
so the ring drains exactly. The buffer is not undersized for one stall; no
sane buffer covers a burst of them. The card had ~29 GB free, so this is not a
full-card effect.

### The replacement card

Same probe on a candidate 58 GB exFAT card, 256 MB written in 32 KiB
WriteThrough chunks (`tools/sd_card_latency_probe.ps1`):

| | old card, in the P4 | candidate, on the PC |
|---|---|---|
| worst single write | 553 ms | **28.95 ms** |
| writes >= 100 ms | 24 | **0** |
| writes >= 360 ms | 8, consecutive | **0** |
| p99.9 | — | 10.56 ms |
| throughput | — | 15.4 MB/s (0.18 needed) |

Two caveats before treating this as settled: the probe ran on a PC host
controller, not the P4's SDMMC, so a clean result is indicative rather than
proof; and the candidate was empty, which is the easy case for a card, whereas
the old one carried accumulated recordings.

**Not yet done, pending an enclosure opening:** put the candidate in the P4 and
repeat the soak, and run the same probe against the old card on the PC so both
numbers come from one tool instead of comparing a PC probe against a P4
measurement.

### Still unexplained

Why a microSD stall stalls the *mixer* loop, which touches no card and holds no
lock, is not established. The trigger is now known; the mechanism is not. If a
better card removes the trigger the question becomes academic, but it should not
be recorded as answered.

## Write staging made it worse, and named the mechanism (2026-07-23)

Hypothesis under test: the recorder calls `fwrite` with 1 KiB blocks while
newlib's default stdio buffer is 128 B, so each block became eight tiny writes
down to FATFS. Small scattered writes are what push a flash controller into
erase-block reorganisation, so accumulating blocks into a 64 KiB staging buffer
and handing the card one large sequential write should have helped.

It did the opposite, decisively:

| | `RC1-191`, no staging | `RC1-195`, 64 KiB staging |
|---|---|---|
| run length | ~25 min | **~3.2 min** |
| worst single write | 553 ms | **1 735 ms** |
| writes >= 100 ms | 24 | **498** |
| worst output block | 356 ms | **913 ms** |
| worst `mix` phase | 356 ms | **730 ms** |

Staging makes writes 64x less frequent, so ~520 writes occurred in that run and
**498 of them blocked over 100 ms** — essentially every one. Reverted in
`cea516e9`.

### Why, and what it explains

The staging buffer was allocated in PSRAM, so each 64 KiB `fwrite` streams from
PSRAM to the SDMMC peripheral. That holds the PSRAM bus for one long continuous
stretch instead of short bursts — and the mixer loop reads its per-deck PCM out
of the same PSRAM. Hence `mix` doubling to 730 ms.

This is the first direct evidence for the mechanism recorded as unexplained
above: **a microSD stall damages the mixer loop because both contend for the
PSRAM bus**, not because of any lock or shared code path. The mixer holds no
lock and never touches the card, which is why the earlier phase breakdown was
so confusing. Larger transfers made the contention worse in exactly the way the
theory predicts, which is the strongest confirmation available without a bus
analyser.

Practical consequence: **do not move recorder bulk I/O into larger PSRAM-sourced
transfers.** If staging is retried, the buffer must live in internal RAM so the
DMA source is not the bus the audio path depends on — but internal RAM is
scarce (~116 KiB free), so that caps the buffer far below 64 KiB and the benefit
is unproven. The card swap remains the first thing to try.

## Reverted-build baseline, 25 min with 5-minute checkpoints (2026-07-23)

Run on `RC1-197-gdf07cdc1` (the staging revert), two decks playing continuously
with tracks reloaded as they ran out, recording throughout.

| checkpoint | late blocks | worst block | worst write | writes >=100 ms | dropped blocks |
|---|---|---|---|---|---|
| T+0 | 120 (inherited) | 322 ms | 5.6 ms | 0 | 0 |
| T+5 | 131 | 322 ms | 374 ms | 14 | 92 |
| T+10 | 159 | 337 ms | 454 ms | 37 | 127 |
| T+15 | 167 | 337 ms | 492 ms | 42 | 127 |
| T+20 | 175 | 337 ms | 492 ms | 57 | 458 |
| T+25 | **184** | **337 ms** | **492 ms** | **80** | **549** |

Note the audio counters are not reset when a recording starts, so the T+0 row
carries 120 late blocks from an earlier run; the meaningful figure is the
increase, **+64 over 25 minutes** (~2.6/min).

549 dropped blocks is 140 544 frames, **3.19 s of audio missing from the
recording**. Losses are bursty rather than steady: 92 blocks by T+5, then flat
through T+15, then +331 in the T+15→T+20 window alone. That is the signature of
a run of consecutive card stalls long enough to outlast the 2.95 s ring, not of
a buffer that is merely a little too small.

Comparison across the 25-minute soaks:

| build | late blocks | worst block |
|---|---|---|
| `RC1-190` | 66 | 320 ms |
| `RC1-191` | 293 | 356 ms |
| `RC1-197` (staging reverted) | 64 | 337 ms |
| `RC1-195` (64 KiB PSRAM staging) | 440 in 3 min | 913 ms |

`RC1-197` lands back with `RC1-190`/`RC1-191`, confirming the staging attempt
was the only regression and that the card remains the cause. Nothing in the
firmware has moved the needle; the card swap is the outstanding experiment.

## Splitting gate wait from card time (2026-07-23, `RC1-200`)

The earlier "the card takes 553 ms to accept 1 KiB" figure was challenged by an
obvious fact: the same card records 1080p60 video, roughly fifty times the
176 kB/s the recorder needs. It turned out the measurement bracketed the whole
sink call, which is `sd_io_gate_begin(); fwrite(); sd_io_gate_end()`, so gate
contention and card time were indistinguishable. `RC1-199` split them.

25-minute soak, two decks playing, recording throughout:

| | T+0 | T+5 | T+10 | T+15 | T+20 | T+25 |
|---|---|---|---|---|---|---|
| `gate_wait` max | 0.0 ms | 8.3 ms | 8.3 ms | **185.0 ms** | 185.0 ms | 185.0 ms |
| `fwrite` max | 17.2 ms | 369.7 ms | 369.7 ms | 369.8 ms | 369.8 ms | 369.8 ms |
| writes >=100 ms | 0 | 4 | 12 | 33 | 41 | 56 |
| dropped blocks | 0 | 0 | 0 | 327 | 327 | 426 |

### Three findings

**1. The stall is inside `fwrite`, not in our gate.** In steady state the gate
wait is 7-8 us. `sd_io_gate` contention is not the cause.

**2. `fwrite` maxima are suspiciously deterministic.** 369 665 / 369 665 /
369 814 / 369 814 / 369 814 us — a 150 us spread across twenty minutes. Random
flash housekeeping does not look like that; this resembles a fixed-cost
operation or a timeout, and is worth chasing before blaming card wear.

**3. The diagnostics amplify the failure — self-inflicted.** `gate_wait` jumps
from 8 ms to 185 ms exactly when stalls begin, because every stall over 100 ms
emits `RECORDING_SD_STALL` and every overrun emits `RECORDING_DROPPED`. The
journal writer then writes those to the *same card* under the *same gate*. More
stalls produce more journal traffic, which produces more stalls. This feedback
loop was introduced with the stall instrumentation earlier the same day and must
be rate-limited or deferred.

### Bursts, and why a camera does not care

Stalls arrive in bursts roughly every 2.2 minutes. Within a burst the writer is
blocked continuously — the interval between stalls equals their duration:

```
ms=1355481  stall=364 ms  ring= 33
ms=1355843  stall=362 ms  ring= 51
ms=1356394  stall=551 ms  ring= 79
...  fifteen consecutive, ~4.5 s total  ...
ms=1359689  stall=365 ms  ring=508   <- full
ms=1359944  stall=255 ms  ring=508   -> dropped
```

This reconciles the card recording 1080p60 with it failing here. A camera
buffers in tens or hundreds of MB — many seconds of video — so a 4.5 s card
stall is invisible to it. The recorder's ring is 508 blocks, **2.95 s**. The same
stall that a camera absorbs drains our buffer completely. The card can be
genuinely fine for video and still unusable for this without a larger buffer or
a card that does not stall for seconds at a time.

### Next, in order

1. Stop the diagnostic feedback loop: rate-limit `RECORDING_SD_STALL`, or queue
   stall records and emit them after the session ends.
2. Test whether the 10 s checkpoint's header patch is the trigger. It seeks to
   offset 0, writes 44 bytes and seeks back, breaking the sequential write
   stream. `recover_orphans()` already rebuilds the header from the file size at
   boot, so the in-session patch is arguably redundant.
3. Only then compare cards, with the probe in `tools/sd_card_latency_probe.ps1`.

## Removing our own contribution: one fix worked, one hypothesis died (2026-07-23)

Two changes went in together as `RC1-202-g05c23a40`, both aimed at things the
firmware was doing to itself rather than at the card:

1. **Stall reporting coalesced.** Every write over 100 ms had emitted a journal
   record, and the journal writer put it on the same card under the same gate,
   so a burst of fifteen stalls added fifteen card transactions precisely when
   the card was already behind. Now one record per burst, drops rate-limited to
   one per two seconds.
2. **The 10 s checkpoint stopped patching the WAV header.** It had seeked to
   offset 0, written 44 bytes and seeked back, interrupting a strictly
   sequential append with a random write at the far end of a large file.

Measured against `RC1-200` at the same point in the run:

| | `RC1-200` | `RC1-202` | |
|---|---|---|---|
| `gate_wait` max | 185.0 ms | **16.5 ms** | fixed |
| `fwrite` max | 369.8 ms | **375.8 ms** | unchanged |

**The diagnostics fix worked.** Gate contention fell by an order of magnitude,
confirming the feedback loop was real and is now gone.

**The checkpoint hypothesis was wrong.** Removing the in-place header patch left
`fwrite` exactly where it was, 375.8 ms against 369.8 ms, which is noise. The
non-sequential 44-byte write was not the trigger. The change is kept anyway —
it removes pointless I/O and `recover_orphans()` already rebuilds the header at
boot — but it is not a fix for the stalls.

### Where that leaves it

Everything the firmware contributes has now been measured and removed:
`sd_io_gate` contention is ruled out (7-8 us in steady state), the diagnostic
feedback loop is closed, and the checkpoint's random write is gone. The ~370 ms
`fwrite` stall survives all of it, still landing within a few hundred
microseconds of the same value run after run.

That leaves the card or the SDMMC layer beneath FATFS. **The card swap is now
the next step and, unlike before, it is the right one**: it is being reached by
elimination rather than by assumption. Qualify the replacement with
`tools/sd_card_latency_probe.ps1` before fitting it, and note the candidate was
probed on a PC host controller while empty, which is the easy case.

Worth carrying into that test: the stall value's determinism. A worn card doing
opportunistic garbage collection would vary; hitting ~370 ms repeatedly looks
more like a fixed-cost operation or a timeout, which would survive a card swap.
If the replacement stalls at the same number, suspect the SDMMC driver or bus
configuration rather than the media.

## FLX4 MASTER OUT as the main output — feasibility probe (2026-07-23)

Question: the FLX4 has two physical outputs. We drive the headphone one over USB
audio and use our own PCM5102A for MAIN. Could the controller's MASTER OUT
replace the DAC?

### The controller already exposes it

The FLX4's USB playback format is **four channels**, and `flx4_usb_audio`
already maps onto it — `fill_next_stream_packet()` writes the P4 monitor mix at
`first_channel = 2` when `channels >= 4`. Channels 1/2 were being zero-filled
every packet. There is even a diagnostic Kconfig switch,
`DDJ_FLX4_USB_AUDIO_TONE_ON_CHANNELS_1_2`, from the original bring-up.

**Confirmed on hardware:** with that switch on, the monitor mix came out of the
FLX4 **MASTER** output. Channels 1/2 are MASTER, channels 3/4 are the
headphones. No new code was needed to establish this.

### The blocker is the P4->S3 link, and it is not fatal

`monitor_pcm_link` carries stereo only — `monitor_pcm_link_write_nonblocking()`
takes `const int16_t *interleaved_stereo`, and the transport rejects anything
but `channels == 2`. Carrying master and cue simultaneously needs four.

The transport is an I2S pipe at `MONITOR_PCM_LINK_I2S_PIPE_RATE_HZ` = 64 kHz
with 16-bit stereo slots, so 2.05 Mbit/s of framing capacity for `P4HP` blocks:

| payload | rate |
|---|---|
| stereo @ 48 kHz (today) | 1.54 Mbit/s — fits |
| four channels @ 48 kHz | 3.07 Mbit/s — **does not fit** |

Widening the slots to `I2S_DATA_BIT_WIDTH_32BIT` doubles the pipe to
4.10 Mbit/s, which carries four channels with margin. Prefer that over raising
the pipe rate: the code already had to drop `mclk_multiple` to 128x because the
P4's 40 MHz XTAL source made 256x abort with "sample rate is too large", so
there is little clock headroom but plenty of slot headroom.

### Sketch of the work, if it is ever wanted

1. `monitor_pcm_link`: 16 -> 32-bit slots, accept `channels == 4`, widen the
   framing.
2. P4 `audio_engine`: send `master_out` alongside `hp_out` instead of `hp_out`
   alone — both blocks already exist at that point in the output loop.
3. S3 `flx4_usb_audio`: fill both channel pairs; the mapping already exists.
4. Optionally drop the PCM5102A, freeing I2S unit 1 — worth something given the
   P4 has only two usable units (unit 2 freezes on eco2).

No wiring changes: same three GPIOs, same cable.

### Recommendation

Feasible, but **not an audio-quality win**. The PCM5102A is a competent
dedicated DAC and the FLX4 is an entry-level controller; expect parity at best.
The real gains are fewer boxes and cables, master and cue coming from one
device, and a freed I2S unit. Two costs to weigh: master would gain the USB
isochronous path's buffering latency that the direct RCA path does not have,
and the FLX4's physical MASTER LEVEL knob would sit after our limiter and
master trim, so the two gain stages would interact.

Reverted after the probe — the switch routes the monitor mix to master, which
leaves the headphones silent.

## Reboot on Wi-Fi enable — instrumented, two suspects ruled out (2026-07-23)

Reported symptom: the P4 occasionally reboots when Wi-Fi is switched on from
Settings. The journal held four `reset=PANIC` boots out of the last fifteen, so
this is regular rather than rare.

### Why the journal could not answer it

Two reasons, and the second is a general trap worth remembering.

**There was no Wi-Fi instrumentation at all.** Not one event in the inventory
covered enable, start, failure or stop — zero hits searching the whole log. The
reported symptom was invisible by construction.

**A panic destroys the unflushed journal buffer.** The writer syncs at most
every few seconds, so the last record before a panic is simply where the buffer
was severed, not where the fault was. All four PANIC boots end on something
unrelated — a `TRACK_LOAD_START` with no matching `DONE`, a
`PROFILE_TRANSFER_DONE` then silence. Reading those as clues would point
straight at the wrong subsystem. **Never treat the last record before a panic as
evidence unless it was explicitly synced.**

Also checked and clear: `LOW_INTERNAL_HEAP` and `LOW_PSRAM` have never fired in
the entire log, so this is not a slow leak.

### What was added

`RC1-205` adds `WIFI_ENABLE_REQUESTED` / `WIFI_STARTED` / `WIFI_FAILED` /
`WIFI_STOPPED`, and the enable path calls `service_log_sync()` **before**
touching the Wi-Fi stack so the breadcrumb reaches the card even if the next
call panics. Records carry free internal heap and the largest free internal
block; completion records carry the worker task's stack high-water mark.

### First measurement already eliminates both suspects

```
WIFI_ENABLE_REQUESTED  a0=160431  a1=92160   free / largest block, before
WIFI_STARTED           a0=135263  a1=4044    free / stack words left, after
```

**Stack exhaustion is out.** The worker is a 6 KiB task and roughly 4 KB of that
is still unused after bringing up ESP-Hosted, Wi-Fi and httpd. Enlarging it
would have been wasted effort.

**Memory pressure is out.** Bring-up costs about 25 KB of internal RAM
(160431 -> 135263) against a 92 KB largest free block going in. Nowhere near
exhaustion.

### How to read the next occurrence

- `WIFI_ENABLE_REQUESTED` with no `WIFI_STARTED` → the panic is inside the
  Wi-Fi/ESP-Hosted bring-up itself.
- both records present, reboot later → Wi-Fi is a trigger, not the fault; look
  at what the newly-started web server, DNS server or the C6 radio disturbs.
- neither record → the panic precedes the request, so the Settings/UI path is
  implicated rather than the radio.

Coredump remains unavailable on this board (it boot-loops before `app_main`),
so a live COM15 capture during the toggle is the fallback if the journal
breadcrumbs are not decisive.

## 2026-07-24 — Beat FX Flanger: tuned by ear, defect found by measurement

Deployed in `RC1-221-g06516945` over Wi-Fi OTA (no wired serial available).

### The tuning that was accepted

| parameter | before | after | why |
| --- | --- | --- | --- |
| wet max | 0.50 | 0.70 | notch depth is `20*log10(1-wet)`: -6 dB vs -10.5 dB |
| minimum delay | 600 us | 250 us | first notch is `1/(2*delay)`: 833 Hz vs 2 kHz |
| feedback max | 0.60 | 0.75 | sharpens the resonance into the jet |
| output normalisation | `1/(1+wet)` | removed | made the depth knob quieter, not more intense |

0.90 wet was tried and rejected as choked; 0.86 feedback was rejected as worse,
not stronger.

### The part that was not in the DSP

After all four changes the operator still heard only "jet na pola". It came
good on a **faster BEAT setting**. A slow sweep spreads the resonance over so
long a period that it reads as tonal drift rather than movement. Test the beat
selector before concluding an effect is mistuned.

### Two measured dead ends

- **Automated swing metric.** Built to rank configurations objectively; it
  ranked the worst-sounding one highest. Discarded.
- **Feedback low-pass.** Added to bound the resonance, then measured as doing
  nothing: peak stayed at 3.34x byte for byte. It attenuates highs while the
  resonant peak sits at 400-1000 Hz, below the cutoff. Removed rather than kept
  as decoration.

### Clipping defect, and why the ear missed it

Measured resonant gain of the accepted tuning is **3.34x** (theoretical
`1 + wet/(1-fb)` = 3.8x). The test track peaks near 16% of full scale, so the
output never approached the ceiling during the listening pass. At a realistic
loud level it does, and it hard-clips *inside* the effect - ahead of the master
limiter, so nothing downstream can catch it. `/api/status` corroborated it after
the session: `limiter_samples=5`, `limiter_peak=32189`.

Fixed with a quadratic soft knee at 0.75 FS on both the output and the feedback
write:

```
                      before fix        after fix
 400 Hz, in  6% FS    3.34x             3.34x, nothing pinned  (identity)
 400 Hz, in 49% FS    2.05x, clipped    2.04x, reaches FS, nothing pinned
 400 Hz, in 79% FS    -                 1.26x, saturates gently
```

Below the knee it is the identity, so the accepted tuning is bit-exact at the
levels it was judged at. Soft-clipping the feedback write also explains why
raising feedback past 0.85 sounded worse rather than stronger: the hard clamp
squared off the recirculating signal and the distortion fed back on itself.

## 2026-07-24 — Echo and Delay had the same headroom defect

Deployed in `RC1-223-gdfa619a9`.

The operator accepted both by ear on hardware — "oni su dobri, provjereno" —
and no DSP or mapping change was needed for how they sound. Measurement then
found the same defect the flanger had, for the same reason it went unheard
there: the reference track peaks near 16% of full scale.

Both share the flanger's structure, dry at unity with wet added on top, so a
sustained signal builds to `1 + wet/(1-feedback)`.

| effect, full depth | wet | feedback | peak gain | samples pinned at 49% FS in |
| --- | --- | --- | --- | --- |
| ECHO | 0.70 | 0.68 | 3.18x | 250400 of 529200 (47%) |
| DELAY | 0.70 | 0 | 1.70x | 0 (clips above ~59% FS in) |

47% of ECHO's output samples squared off against the int16 ceiling — inside the
effect, ahead of the master limiter, where nothing downstream can catch it.

Same quadratic soft knee at 0.75 FS as the flanger, on the output and the
feedback write. After:

```
  ECHO   400 Hz at 49% FS : 236950 -> 0 pinned
  ECHO   100 Hz at 49% FS : 250400 -> 0 pinned
  DELAY  400 Hz at 79% FS :  72100 -> 40600 pinned (saturates gently instead
                                                    of squaring off; 1.7x over
                                                    full scale has to go
                                                    somewhere)
```

Quiet case identical before and after: 3.18x / 3.10x / 1.70x.

### The lesson worth keeping

Twice in one session a listening pass passed an effect that measurement then
failed, both times because the reference material never approached the ceiling.
**An ear acceptance does not cover headroom.** Any effect that adds a wet signal
on top of unity dry should have its peak gain measured before it is called done
— it is a two-minute host probe, and the failure it catches is inaudible until
someone plays a loud track.

## 2026-07-24 — Recorder shelved (compiled out by default)

`CONFIG_AUDIO_RECORDER_ENABLED`, default `n`. Nothing deleted; the component,
its host tests and the UI/API surface stay in the tree, only the wiring is
gated. Binary is 12,976 bytes smaller with it off.

### Why

The recorder needs 176 kB/s and cards deliver ~12 MB/s, so throughput was never
the problem. The problem is a card that stops answering for hundreds of
milliseconds while it does internal housekeeping; a burst of those drains the
2.95 s ring regardless of how the writer is arranged.

Every firmware-side contribution was eliminated first, and none was the cause —
see the "Why it was shelved" table in `DEVELOPMENT_PLAN.md`. After all of it,
~370 ms of `fwrite` survived while `gate_wait` fell to single-digit ms: the
firmware was no longer in the way at all.

### What the new card measurement actually showed

A replacement card was fitted and the soak was **cut short by the decision to
shelve the feature**, so there is no completed 25-minute run for it. What exists
is the first five minutes:

```
T+0    fwrite  72.9 ms   ring 13/508   drops 0
T+5    fwrite  86.3 ms   ring 27/508   drops 0
```

Encouraging against the old card's ~370 ms, but **five minutes is not a result**
— cards begin stalling once they fill and internal remapping starts, which is
precisely why the protocol is 25 minutes. Do not cite these numbers as evidence
the card swap fixed anything.

The old card was measured separately on a PC (`tools/sd_card_latency_probe.ps1`,
256 MB in 32 KiB WriteThrough chunks):

```
median 1.93 ms   p99 8.86 ms   p99.9 39.03 ms   MAX 1415 ms
>=360 ms: 1      throughput 11.9 MB/s (recorder needs 0.18)
```

One 1.4 s stall on a PC, against bursts of eight ~360 ms stalls seen on the P4.
Same failure mode, rarer on the PC — better power delivery and different request
ordering.

### Two loose ends found during the soak

- **Loading a track killed an in-progress recording.** `deck2 <- track 75` at
  01:39:45, recorder `STOPPED` at 01:39:54. Never diagnosed.
- ~~96 kHz/24-bit FLAC fails to load on deck 1~~ — **withdrawn 2026-07-24, this
  was not a defect.** The load returns `AUDIO_LOAD_FAILED a1=261 msg=NOT FOUND`
  (`ESP_ERR_NOT_FOUND`): the file is listed in the rekordbox PDB but was never
  copied to the USB drive. FLAC is supported (dr_flac, decoded from the PSRAM
  preload). The trap: every non-mp3 entry in this library — indices 0, 1, 2 and
  5, both FLACs and both WAVs — is a dead PDB row, so picking any of them to
  "test FLAC" tests a missing file instead. Use `/api/diagnostic-log` to read
  the actual error code before concluding a format is broken.

## 2026-07-24 — Loop took effect ~2 s late

Reported as "the loop sometimes skips a beat at the beginning". Fixed in
`RC1-229-g6b54fcad`.

### Cause

The decoder runs ahead of playback — measured on hardware, steadily:

```
ring_used1 = 86272-86912 frames = 1.95-1.97 s ahead
```

`audio_engine_deck_set_loop()` sets three fields and nothing else, and the
decode-side wrap seeks back to `loop_start` with `AE_SEEK_REASON_LOOP`, which
deliberately does not flush the ring. So arming a loop whose out point is at or
behind the playhead leaves that whole ~1.96 s lead already published — and it
plays before the loop's first pass. The ring never dips across an arm:

```
PRE   ring_used=86528     >>> ARM loop_4
T+1   ring_used=86528         (no flush)
T+2   ring_used=86016
```

### Why "sometimes" — it is purely tempo

The extra material is always ~1.96 s, so whether it lands on the grid depends
only on the track:

| BPM | beat | 1.96 s in beats | off-grid by |
| --- | --- | --- | --- |
| 108 | 556 ms | 3.53 | 294 ms — half a beat |
| 120 | 500 ms | 3.92 | 40 ms |
| 125 | 480 ms | 4.08 | 40 ms |
| 128 | 469 ms | 4.18 | 85 ms |
| 135 | 444 ms | 4.41 | 183 ms |

That is the whole "sometimes": at 120-125 it sounds nearly right, at 135 and 108
it clearly lurches.

### Fix

The wrap withdraws what it published past `loop_end`. It splits in two because
the push loop runs *after* the wrap check: already-published frames are taken
back from the store, and the current batch is clamped by a separate
`publish_frames` limit.

The trim is **precise, not a flush** — that distinction matters:

- manual LOOP IN/OUT: out point is at the playhead, so the whole runway goes and
  the loop starts immediately, as a CDJ does;
- beat-loop pad / `loop_4`: out point is ahead of the playhead, so only tens of
  ms are outside the loop. A flush here would have discarded nearly two seconds
  of valid in-loop audio and opened a real gap.

It also fixes the waveform playhead drifting out of the loop region: the output
task's bookkeeping wraps on time at `loop_end`, and now the store's wrap point
is exactly there instead of approximately there.

### Verified on hardware

`loop_4` on a 129 BPM track (4 beats = 1860.5 ms expected):

```
position range observed : 1858 ms
wrap periods (ms)       : 1835, 1866, 1870, 1852, 1894, 1851  -> mean 1861.3
output_late_count       : 0
```

Mean is 0.8 ms off the expected length over six passes; the ±30 ms scatter is
the HTTP polling interval, not the loop. A surviving per-pass overshoot would
have pushed the mean to ~1886 ms.

**Not verified:** the manual LOOP IN/OUT case — the severe one, where the whole
1.96 s runway is withdrawn — needs the FLX4, which is not enumerating (see the
bench-power note). `loop_4` only ever had ~56 ms outside the loop, so hardware
testing so far exercises the mild case plus the steady state.

### Follow-up: the trim emptied the ring and clicked (RC1-232-g8f6656cb)

The first version of the trim shipped a regression. Arming a manual loop puts
the out point at the playhead, so the whole decoder lead is genuinely past it
and the trim withdrew all of it — correct in principle, but the decode task
still has to reseek, reinit the MP3 decoder and produce a first batch, and the
output empties the ring before it can.

Neither the late-block counter nor the journal saw it, which is why three
rounds of reasoning failed to find it. Two counters were added and it fell out
immediately:

```
                          before   after
pcm_underrun1              512  ->   0
output_late_count            1  ->   0
output_late_max_us       11694  ->   0

loop_trim_wraps1            14
loop_trim_dropped_max1   83799 frames (1900.2 ms)   one withdrawal, at arm
loop_trim_clamped_total1 15933 (361.3 ms)           13 wraps, ~28 ms each
```

The trim now leaves `AE_LOOP_TRIM_MIN_RUNWAY_FRAMES` (2048, four times the
observed 512-frame shortfall). The arithmetic checks out on hardware: runway
was 85847, withdrawal 83799, difference exactly 2048. Cost is up to ~46 ms of
overrun on the loop's first pass, against the 1946 ms it removed.

**The lesson, and it is the same one as the recorder and the flanger.**
`pcm_underrun_count` already existed in the diagnostics snapshot and was
surfaced nowhere; it counts per-frame pop failures, which is exactly the shape
of a click that leaves no journal record. The trim itself had no accounting at
all. A change to the audio hot path needs a counter for what it does, or its
regressions are only findable by ear — and by then they have shipped.

## 2026-07-24 — Idle screensaver

Shipped in `RC1-237-g7bf0fd3c`; operator confirmed all four behaviours on
hardware. Two-minute timeout, fixed by decision — the plan's Settings entry was
explicitly declined, so `UI_IDLE_DEFAULT_TIMEOUT_MS` stays a constant. The
timing core already takes the timeout as a parameter and treats 0 as Off, so
adding the control later is wiring, not redesign.

### The two parts that were not obvious

**Consuming the event that wakes it** splits in two, and only one half is free.
Touch costs nothing: the screensaver is its own LVGL screen with no widgets, so
a dismissing tap cannot press whatever sits underneath. Controller events do
cost something, because every FLX4 button, jog, fader and web mutation passes
through `deck_core_queue_event()` — that callback returns `bool` and the event
is swallowed when it woke the screen. Without it a PLAY press that wakes the
panel would also start the deck.

`ui_activity_notice()` only sets a flag and reports whether the screensaver was
up; the LVGL work happens on the UI task's next 16 ms tick, so
`deck_core_queue_event()` stays lock-free on whatever task produced the event.
Events arriving inside that window are also swallowed, which reads as debounce.

**Restoring the screen is not enough.** LVGL repaints the whole tab on return
and erases the direct-PPA waveform strips — the same failure the 2026-07-09
stability pass fixed for tab switching. `ui_overview_note_screen_restored()`
forces that existing recovery rather than adding a parallel one.

### Design points worth keeping

Playback and recording are treated as *activity*, not merely as a veto. Without
that, a deck playing past the timeout would blank the UI the instant the track
ended — precisely when the operator is looking at it. Changing the timeout
likewise restarts the countdown instead of back-dating it.

The caption is 24 px at `0xB0B0B0`; 16 px at `0x808080` was reported as barely
visible at the distance the panel is actually read from. It is static because
the wordmark already animates nine labels and this panel's invalidate budget is
delicate.

Host tests cover the show edge firing once, dismissal restarting the full
countdown, playback inhibiting and re-arming, recording hiding an active
screensaver, the Off position, the timeout-change case, and the 49.7-day
millisecond wrap.
