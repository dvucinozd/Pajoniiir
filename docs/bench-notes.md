# Bench Notes

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
| ESP32-C6 hosted Wi-Fi firmware | P4 MVP build | P4 initializes hosted Wi-Fi without error | **PENDING** | Verify C6 image compatibility before judging AP behavior |
| Web UI SoftAP | P4 startup | SoftAP starts without a user-selectable link mode | **PENDING** | P4 now starts hosted AP unconditionally for web UI/captive portal |
| Captive portal HTTP | Phone/PC client | `/`, `/api/status`, `/api/library`, `/api/load` respond on AP IP | **PENDING** | Current user-facing network path |
| Captive DNS | Phone/PC client | arbitrary DNS queries resolve to the P4 AP IP without malformed-packet crash | **PENDING** | Uses bounds-checked DNS reply builder |
| Concurrent web load | Browser double-click/load spam | Second `/api/load` is rejected while load worker is busy | **PENDING** | Prevents concurrent P4 track load workers |
| Remote cache re-enable | Future CDJ Link build | STA/join, UDP discovery, remote library fetch and SD cache are revalidated | **PARKED** | The old Settings link mode and Library JOINED selector are removed from the current MVP |

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
| Stability under load | **PASS** | MUST preload MP3 to PSRAM + decode via `fmemopen`; streaming from USB during playback trips a USB-DWC channel assert (`usb_dwc_hal.c:502`) → reboot |
| Decode task stack | Note | minimp3 needs ~26 KB → dedicated 32 KB decode task |
| Output routing switch | **PASS** | SETTINGS speaker⟷RCA toggle drives PA (GPIO11); RCA mode mutes speaker (PA off), clean line-level on DAC_OUT_P/N |
| Settings persistence (NVS) | **PASS** | `app_settings` stores audio out / backlight / time mode; power-cycle restored audio_out=1 (RCA) + backlight=46 and applied at boot |
| PWM backlight | **PASS** | LEDC 10-bit @ 5 kHz on GPIO23; SETTINGS slider smoothly dims/brightens the panel |
| Line-level output viable | Pending | verify ES8311 DAC_OUT_P/N → RCA jack wiring + line level in the chassis (board has speaker amp, RCA tap to be wired) |

---

## CDJ Link / ESP-Hosted Bring-Up

| Check | Result | Notes |
| --- | --- | --- |
| P4 flash target | **PASS** | `COM15`, ESP32-P4 rev v1.3, MAC `80:f1:b2:d0:b4:9b` |
| ESP-Hosted SDIO pins | **PASS** | Log confirms slot 1, 4-bit, CLK 18, CMD 19, D0-D3 14-17, C6 reset 54 |
| Host transport init | **PASS** | C6 identified as `esp32c6`; SDIO card init successful; transport active |
| SoftAP host mode | **PASS / HISTORICAL** | Previous build used NVS `link_mode=1` to start SoftAP on `192.168.4.1`; current firmware starts host AP directly for web UI/captive portal |
| Remote library snapshot | **PASS** | USB mounted and `cdj_link_server` built snapshot: 308 tracks, 73320 bytes |
| Windows Wi-Fi scan | **PASS** | PC sees the `CDJ100S-<id>` SoftAP at strong signal; WPA2 profile connects with `cdj100slink` |
| HTTP hello/library | **PASS** | `GET /v1/hello.txt` returns `CDJLINK1`; `GET /v1/library.bin` returns 73320 bytes with 308 records in ~0.13 s from a Windows Wi-Fi client |
| HTTP manifest/DAT | **PASS** | First track `track_key=5` returns manifest: audio 9047128 bytes, DAT 9836 bytes, EXT 212819 bytes; `ANLZ0000.DAT` download matches 9836 bytes |
| HTTP MP3 transfer | **WARN** | Full `audio.mp3` for `track_key=5` downloads successfully after replacing chunked sequential file send with fixed-length pipelined read/send. Best `curl.exe` run measured 9047128 bytes in 9.40-9.42 s (~0.96 MB/s), but subsequent Windows-client runs varied around ~0.75 MB/s and one transfer reset; continue validation on the real joined P4 client. |
| Host busy handling | **PASS** | Host returns `503 BUSY` while USB gate is occupied; client cache now retries `HOST BUSY` with backoff instead of failing immediately. |
| ESP-Hosted mempool | **FIXED** | Initial HOST boot asserted in `sdio_mempool_create` because SDIO mempool allocated ~48 KB internal DMA RAM. Enabling `CONFIG_ESP_HOSTED_MEMPOOL_PREFER_SPIRAM=y` fixes boot on this P4 workload. |
| C6 firmware version | **FIXED** | Upgraded onboard C6 over `COM12` to ESP-Hosted slave `2.12.8` using USB-TTL on `PROG_C6`. Boot log now identifies `esp32c6`, reports `Transport active`, and no longer prints the Host/Co-proc version mismatch warning. |
| C6 slave firmware build | **PASS** | Built and flashed `firmware/main-deck-p4/managed_components/espressif__esp_hosted/slave/build/network_adapter.bin`; stable flashing required P4 held in bootloader and C6 flashing at 115200 baud because 460800 stopped responding through the jumper wiring. |
| SD cache mount | **FIXED** | `/sd` now mounts on boot with the inserted `SA32G` 29.5 GB card: 4-bit SDMMC, 20 MHz. Root cause was missing vendor SD power control: on-chip LDO channel 4 must be attached to `host.pwr_ctrl_handle` before `esp_vfs_fat_sdmmc_mount()`. |

---

## Open Items
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
- ✅ `bsp_sd_init()` SDMMC (config/cache): `/sd` mounts on the JC4880 TF slot. Root cause of the
  earlier timeout was `SDMMC_HOST_DEFAULT()` selecting slot 1 while this board is wired to slot 0.
- Line-level output: verify ES8311 DAC → RCA line level.
- ✅ Display tearing fix — DPI triple buffering wired into the flush (PPA → non-displayed fb,
  then draw_bitmap flips at frame boundary). Verified on hardware: no tearing.
- UI polish — visual/interaction refinement across the 7 screens (open, scope TBD).
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
- **Deferred to S3/chassis phase:** physical CDJ controls → `deck_core` queue; Beat LED feedback
  (PQTZ → S3 LED); wire CDJ front panel to the S3 per `PINOUT.md`; mount display in the CDJ-100S opening.
