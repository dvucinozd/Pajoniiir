# CDJ100S-XXX Development Plan

> Historical note: this is the pre-DDJ-FLX4/CDJ100S chassis plan retained from
> the imported project. For the active DDJ-FFL4 roadmap, use
> [`DEVELOPMENT_PLAN.md`](DEVELOPMENT_PLAN.md).

**Goal:** Standalone single-deck DJ player inside a Pioneer CDJ-100S chassis.  
ESP32-S3 handles CDJ panel controls + USB MIDI; ESP32-P4 handles UI/audio/media.

**Status (2026-05-25):**
- S3 firmware: ✅ V1 running (13 buttons, jog, pitch, 4 LEDs, USB MIDI, heartbeat)
- P4 firmware: ✅ V1 / beta1 running on hardware (DSI Display + touch, USB media library with PDB/ANLZ, custom gapless loop, 1ms Instant Frame-Index seek for Hot Cues & Beat Jump, SRAM I8 Zoom & Static Waveform Canvas with beatgrid alignment, S3 Beat LED feedback, NVS settings persistence).

---

## Current Priority Order (P4-first)

Re-prioritized 2026-05-22: finish everything the **P4 can do standalone** (touchscreen + USB
drive, no S3 board needed), and defer the physical S3 hookup + chassis to the end. The whole
control path is developed and tested on the P4 via touch; the S3 later just becomes another
event source feeding the same `deck_core` → `audio_engine`.

**Priority — P4 standalone:**

- **P1 — `deck_core` ↔ `audio_engine`, driven from the UI (touch). ✅ implemented**
  `deck_core` now drives the engine: PLAY→play/pause, CUE→seek/set, EJECT→stop, jog→seek,
  pitch→`set_pitch`. UI PLAY posts a `BTN_PLAY` event via the new `deck_core_queue_event()`
  (unified control path; S3 will feed the same queue later). Seek runs on the decode task's
  large stack (the `seek_linear` stack concern is resolved).
- **P2 — Header + waveform track real playback. ✅ implemented**
  `deck_core_get_state()` reads live position from `audio_engine_position_ms()`; the header time
  and waveform playhead follow real playback.
- **P3 — Real ANLZ data in the UI. ✅ implemented**
  Hot-cue pads use real PCOB cues, beat jump uses the PQTZ beatgrid, and the scrolling waveform
  shows beatgrid alignment.
- **P4 — Loop playback. ✅ implemented**
  `audio_engine_set_loop/clear_loop/get_loop_state`; auto-seek to loop_in at loop_out; LOOP
  screen wired.
- **P5 — Cut the ~1–3 s load-to-play delay. ✅ done.** P5a "LOADING NN%" indicator + load-state
  API, and P5b progressive preload (loader task + gated decoder) → playback starts in ~0.3 s
  instead of 1–6 s while the rest streams in the background. Verified on hardware (clean audio,
  no underrun). Read throughput measured at ~1 MB/s (P5c).
- **P6 — Lower priority P4: 🟡 in progress.**
  - [x] Audio output routing: SETTINGS switch speaker ⟷ RCA line-out (BSP owns the PA pin GPIO11;
    SPEAKER = PA on, RCA = PA off → clean line-level only). Verified on hardware.
  - [x] Persist settings in NVS (`app_settings` component): audio output, backlight %, time mode;
    applied at boot. Verified on hardware (survives power-cycle).
  - [x] LEDC PWM backlight (10-bit @ 5 kHz on GPIO23): `bsp_display_set_backlight(pct)`; SETTINGS
    slider drives it live, saved value applied at boot. Verified (smooth dimming).
  - [x] `bsp_sd_init()` software path: SDMMC GPIO39–44 mounts FATFS at `/sd` for config/cache;
    non-fatal boot if no card. Hardware-verified on the JC4880 TF slot; board uses P4 SDMMC slot 0.
  - [x] Display tearing fix: DPI triple buffering wired in the flush path — the PPA rotates each
    frame into a non-displayed framebuffer, then `esp_lcd_panel_draw_bitmap()` flips the DPI to it
    at the next frame boundary. Verified on hardware (no tearing on the scrolling waveform/tabs).
  - [ ] Verify the actual RCA wiring + line level (DAC_OUT_P/N → RCA jacks) — needs the chassis.
  - [x] On-screen beat indicator: 4-beat pulse row in OVERVIEW, driven by PQTZ beatgrid with BPM fallback.
  - [ ] **UI polish** — visual + interaction refinement across the 7 screens: consistent
    spacing/typography/colour, tab-switch and button feedback, touch target sizing, waveform
    rendering quality, empty/loading states, and overall layout cleanup. (Scope TBD per screen.)
    - [x] A-1: English labels everywhere + universal press feedback (dim-on-press style).
    - [x] A-2: centralised chrome colour palette into `components/ui/ui_theme.h` (`COL_*` tokens).
    - [x] Fixed blank header BPM/pitch — LVGL builtin `%f` is unsupported; format decimals via
    integer math. (See P4 CLAUDE.md "LVGL `%f` ne radi" warning.)
    - [x] Header: BPM/pitch block pulled to the right edge; added a second time counter showing
      time **remaining** (`-MM:SS.cc`) beside the elapsed one, colour-coded yellow ≤30 s / red
      ≤10 s to the end. Removed the now-redundant elapsed/remaining click-toggle.
    - [x] Hot Cues + Loop: retained separate focused screens; cue pads now use semantic
      green/amber/dim states, Loop shows active/inactive status, and Exit Loop uses the
      shared destructive visual language.
    - [x] Settings: reorganized into an operational two-column screen with
      controls on the left and live system status on the right, plus the Wi-Fi
      remote switch (default off).
    - [x] Beat Jump: retained the dedicated screen and existing jump values while
      replacing the explanatory header with clear backward/forward lanes and
      restrained red/green performance buttons.
    - [x] Key Shift: retained the dedicated screen and existing keylock switch
      behavior while restyling Master Tempo and Key Transpose as clear
      two-panel control/readout surfaces.
    - [x] Footer tabs: retained the seven-tab navigation model while refining
      active, normal, pressed and future disabled states into a clearer
      performance-bar footer.
    - [x] Empty/loading/error states: normalized Library and load/cache status
      vocabulary with consistent action, success, warning and error colors.

- **P7 — Wi-Fi web UI mobile controller: 🟢 re-enabled behind a Settings switch (default off).**
  - [x] **exFAT/GPT USB support for newer AlphaTheta/rekordbox exports.**
    Implemented via `usb_media_mount` (MBR/GPT/superfloppy base-LBA translation)
    + vendored FatFs with `FF_FS_EXFAT=1`; exFAT large reads are chunked into
    ≤64-sector SCSI commands. Hardware-verified: MP3/WAV/hi-res FLAC play from an
    exFAT drive.
  - [x] `wifi_link`: ESP32-P4 hosted Wi-Fi SoftAP re-enabled 2026-07-04. Parked
    2026-06-29 for RF-interference-free development, then un-parked for the web
    UI mobile controller. The onboard ESP32-C6 provides Wi-Fi over ESP-Hosted
    SDIO; `wifi_link_init()` starts SoftAP `PAJONIIR` on 192.168.4.1 and
    `app_main` brings up `web_server`/`dns_server`. Hardware-verified: AP
    appears, web UI loads and controls work. C6 mempool prefers SPIRAM to keep
    hosted buffers out of scarce internal RAM (static DIRAM ~44% used).
  - [x] `media_io_gate`: global USB read gate wrapped around `library_init`,
    `library_load_anlz`, `library_load_current_anlz`, and firmware audio preload reads.
  - [x] `media_catalog`: local USB catalog facade for the library UI.

> **Status note (2026-05-22): P1–P4 verified on hardware** via touch. PLAY/PAUSE, hot cues
> (real PCOB positions), beat jump, and header/waveform position-tracking all work. Loop was
> initially broken (the loop wrap flushed the ring → gap + early/short wrap, worst on ~1-beat
> loops); fixed to be gapless by not flushing the ring on loop-wrap seeks. A **CUE button is now
> on the OVERVIEW touch screen** (right of the upper waveform, level with PLAY/PAUSE) driving the
> `deck_core` BTN_CUE path (set cue when paused / return-to-cue when playing) — verified on hardware.
> The physical S3 CUE control will reuse the same path in the S3 phase.

**Deferred to the end — S3 + chassis:**

- Physical CDJ controls (PLAY/CUE/jog/pitch fader) as an event source into `deck_core`.
- Beat LED feedback back to the S3.
- Phase 9: wire the CDJ-100S front panel to the S3, mount the display, power/audio routing.

---

## P5 — Load-Latency Implementation Plan

Today `ae_decode_task` allocates a PSRAM buffer for the whole file, reads the **entire MP3 from
USB** (256 KB chunks), and only then latches the sample rate, opens the codec, and starts
decoding. That full read (~1–3 s) is the whole delay — playback can't start until the last byte.

> **Implemented (2026-05-22):** P5a (`7898e8e`) and P5b (`a223f5f`) shipped and verified on
> hardware. The actual design differs slightly from the draft below: a simple `s_loading` flag +
> `s_load_pct` drives `audio_engine_get_state()` (state derived from `s_eng.playing`), and the gate
> uses `file_pos + AE_LOAD_GATE_MARGIN(32KB) > s_loaded_bytes` against the loader watermark with the
> total file_size kept for EOF. Result: load→play ~0.3 s (was ~6 s at the measured 1 MB/s read).

### P5a — LOADING indicator + load-state API  *(quick, low-risk — do first)* ✅
- `audio_engine.h`: `typedef enum { AE_IDLE, AE_LOADING, AE_READY, AE_PLAYING } ae_state_t;`
  `ae_state_t audio_engine_get_state(void);` (+ optional `uint8_t audio_engine_load_progress(void)` 0–100).
- `audio_engine.c`: `volatile ae_state_t s_state` + `volatile uint8_t s_load_pct`. `load()` → LOADING;
  update `s_load_pct` in the preload loop; after codec open → READY; `play()` → PLAYING; `stop()` → IDLE.
- `ui.c` (`ui_update`): while LOADING show **"LOADING… NN%"** in the header (and optionally dim PLAY);
  on LOAD tap set the title + "LOADING…" immediately for instant feedback.
- Risk: low (read-only state + UI text).

### P5b — Progressive preload  *(the real latency cut)* ✅
- Split the producer into a **loader task** (small stack; reads USB→PSRAM, bumps
  `volatile size_t s_loaded_bytes`; the only USB user) and the **decode task** (32 KB; decodes from
  `file_buf` into the ring, **gated** so `decode_one_frame` only runs while
  `file_pos + window <= s_loaded_bytes`, else waits — never touches USB).
- Flow: alloc PSRAM (size from `stat`) → start loader → once ~256–512 KB is in, latch rate + open
  codec + READY → decode loop starts (gated). Playback begins in ~0.2–0.5 s instead of 1–3 s.
- **Safety (no USB-DWC crash):** `library_load_anlz` (ANLZ read) runs on the LVGL task and finishes
  *before* `audio_engine_load`; a new load `stop()`s the previous loader first. So only one task
  reads USB at any moment → no concurrent transfers (the thing that tripped `usb_dwc_hal`).
- Edge cases: a seek (hot cue/beat jump) past `s_loaded_bytes` waits for the loader (brief; the whole
  file lands within 1–3 s anyway); seek/loop inside the loaded region is instant.
- Risk: moderate (new task, gating, seek-vs-watermark) — all contained in `audio_engine.c`.

### P5c — Measure / tune read throughput  *(optional)* ✅ measured
- Measured **~1 MB/s** USB MSC read throughput (e.g. 4090 KB in 3926 ms). That's the bottleneck,
  but P5b hides it (loader ~40× faster than playback consumption), so playback no longer waits for
  it. Further read-speed tuning (FATFS cluster / MSC transfer size) is a possible future win but
  not needed now.

**Order:** P5a → P5b → P5c as needed.

---

## Phase 0: Bench Inventory ✅ COMPLETE

- [x] Select ESP32-S3-DevKitC-1 N16R8 as control board
- [x] Confirm GPIO assignments for 13 buttons, jog encoder, pitch ADC, 4 LEDs, UART link
- [x] Flash S3 via COM4 (CH343 UART bridge, GPIO43/44)
- [x] Confirm USB MIDI enumeration on COM5 (VID_303A:PID_4008)
- [x] Document all S3 smoke tests in `docs/bench-notes.md`
- [x] Fill `docs/wiring-map.md` with confirmed GPIO assignments
- [x] P4 hardware received (JC4880P443C_I_W) and all smoke tests passing
- [x] Verified P4 hardware stability under high USB media loads

**Exit:** `docs/bench-notes.md` S3 and P4 sections complete.

---

## Phase 1: Project Skeletons ✅ COMPLETE

- [x] ESP32-S3 firmware skeleton (`firmware/control-board-s3/`)
- [x] ESP32-P4 firmware skeleton (`firmware/main-deck-p4/`)
- [x] ESP-IDF v5.5 confirmed working for both targets
- [x] `sdkconfig.defaults` for S3 with all critical settings
- [x] `idf.py build` passes for both firmware projects

---

## Phase 2: Board Support Package 🟡 PARTIAL

Files: `firmware/main-deck-p4/components/bsp_jc4880/`

- [x] Display init for ST7701S (MIPI DSI), backlight GPIO23 — verified on hardware
- [x] GT911 touch on GPIO7/8, address 0x5D — verified on hardware
- [x] USB host for USB drive (media source via ESP-IDF USB Host MSC → VFS at `/usb`) — verified on hardware
- [x] ES8311 codec + I2S init (I2C GPIO7/8 @0x18, I2S GPIO9/10/12/13/48, PA GPIO11) — verified, audio plays
- [x] SDMMC mount (GPIO39–44, config/cache) — verified at `/sd` on the JC4880 TF slot

---

## Phase 3: ESP32-S3 Panel I/O ✅ COMPLETE

- [x] 14 buttons with debounce (active-low, internal pull-up), including LOAD on GPIO21
- [x] Jog encoder (PCNT X4 quadrature, 1µs glitch filter, GPIO15/16)
- [x] Browse encoder (PCNT X4 quadrature, 1µs glitch filter, GPIO17/18)
- [x] Pitch ADC (ADC1 CH0 GPIO1, 14-bit, deadzone ±200, center=8192, invert)
- [x] 4 LEDs (GPIO33/34/38/39, active-high, 220Ω resistor)
- [x] USB MIDI device (TinyUSB, Ch1 buttons+pitch, Ch2 jog, Ch3 browse)
- [x] UART1 control link to P4 (GPIO40/41, 115200 baud, 7-byte frames)
- [x] All V1 smoke tests passing — see `docs/bench-notes.md`

---

## Phase 4: Control Link + Deck Core ✅ COMPLETE

Files: `firmware/main-deck-p4/components/control_link/`, `firmware/main-deck-p4/components/deck_core/`

- [x] P4 UART receiver (`control_link`) — parses 7-byte frames from S3 on GPIO28 RX
- [x] `deck_core` — play/pause, cue set/return, jog nudge/scratch, pitch
- [x] LED feedback from deck_core → S3 LEDs via control_link on GPIO29 TX
- [x] `ctrl_event_queue` — deck_core creates, control_link fills
- [x] Verified end-to-end communication with real S3 board

---

## Phase 5: Deck State Machine ✅ COMPLETE

- [x] PLAY/PAUSE toggle with LED feedback
- [x] CUE point set / return-to-cue
- [x] JOG: nudge when playing, scratch when paused
- [x] PITCH: signed 14-bit value mapped to speed factor
- [x] Performance mode cycle (BTN_MODE)
- [x] MASTER TEMPO toggle
- [x] EJECT track

---

## Phase 6: Media Library ✅ COMPLETE

Files: `firmware/main-deck-p4/components/library/`

### Pioneer PDB parser — DONE ✅

- [x] `rekordbox_pdb.c/h` — `pdb_open()`, parses Tracks + Artists + Albums tables
- [x] `library_track_t` extended: `title`, `artist`, `album`, `anlz_path`, `track_id`
- [x] `library_init()` — opens `export.pdb`, builds index in SPIRAM; no ANLZ walking needed
- [x] PC test harness (`tests/rekordbox_pdb/`) — 9/9 tests pass
- [x] Validated on real USB drive: 154 artists, 308 tracks, 308/308 valid ANLZ paths
- [x] SPIRAM enabled: `CONFIG_SPIRAM=y` + `CONFIG_SPIRAM_USE_MALLOC=y`
- [x] **Optimized library with pointer-based lookup (`library_get_ptr`) to avoid massive stack copying.**

### Rekordbox ANLZ parser — DONE ✅

- [x] `anlz_parse_dat()` — PPTH, PVBR (VBR seek), PQTZ (BPM/beatgrid), PWAV (waveform), PCOB (cues)
- [x] `anlz_parse_ext()` — PWV3 (high-res waveform)
- [x] `library_load_anlz()` uses `track->anlz_path` directly (no directory scan)
- [x] PC test harness (`tests/anlz/`) — 32 unit tests pass
- [x] Validated on real Rekordbox USB drive (308 tracks)

### USB host + live indexing — DONE ✅

- [x] Mount USB drive via USB host ESP-IDF USB MSC → VFS at `/usb`
- [x] `library_init()` opens `export.pdb` automatically on USB insertion
- [x] **Fast and stable loading: 308 tracks parsed and indexed in 261 ms**
- [x] Browse encoder (V2: GPIO17/18) navigates track list in UI

---

## Phase 7: Audio Playback ✅ COMPLETE

Files: `firmware/main-deck-p4/components/audio_engine/`, `bsp_jc4880/`

- [x] ES8311 codec + I2S init (esp_codec_dev; MCLK13/BCLK12/WS10/DOUT9, PA GPIO11)
- [x] MP3 decode (minimp3) → ES8311; stable across many loads + 44.1/48 kHz switching
- [x] Preload track to PSRAM + decode via direct memory mapping (avoids USB-DWC crash from streaming)
- [x] UI LIBRARY "LOAD TRACK" loads `/usb<path>`
- [x] Play/pause/seek/pitch driven through `deck_core` (from touch now; S3 feeds same queue later)
- [x] **Instant Frame-Index (IFI) Seek**: Custom fast MP3 frame header parser (indexes 20k frames in 23ms) provides instant 1ms seek response for Hot Cues & Beat Jump, replacing slow linear seek and buggy `fmemopen` fallback.
- [x] Loop playback: `audio_engine_set_loop/clear_loop/get_loop_state`, auto-seek at loop_out (gapless, sample-accurate inside decode task)
- [x] Hot cues from PCOB + beat jump from PQTZ beatgrid wired to seek
- [x] **Hardware re-verification of the control path** (touch PLAY/CUE/loop/hot-cue/beat-jump → audio) — 100% verified, responsive, clean.
- [x] **LED feedback**: Beat LED pulses sent via UART to S3 on beat grid hits (PQTZ).
- [x] **Zero latency preload**: loading MP3 from USB takes ~0.3s.

---

## Phase 8: LVGL DJ UI ✅ COMPLETE

Files: `firmware/main-deck-p4/components/ui/`

### UI implementation — DONE ✅

- [x] 800×480 landscape root layout with header + footer tab bar
- [x] Header: track title, artist, dual time counters (elapsed + remaining with ≤30 s/≤10 s yellow/red warning), BPM, pitch % (right-aligned)
- [x] Tab 0 — OVERVIEW:
      - **Static Overview Waveform**: High-resolution 1:1 Rekordbox PWAV canvas (400x76 px in I8 SRAM, premium blue color) with zbijeni beatgrid in the background and dynamic upcoming/played coloring.
      - **Scrolling Zoom Waveform**: Zbijeni 60-bar high-res zoom canvas (758x120 px in I8 SRAM) with fine beatgrid alignment.
      - Playhead, PLAY/PAUSE + CUE buttons, tap-to-seek on both waveforms (upper = absolute/coarse, lower zoom = relative/fine).
- [x] Tab 1 — LIBRARY: track list from `library_get_ptr()`, 8 visible songs with fixed 36px height, Montserrat 16 font, and persistent neon blue highlight. Load on select.
      - **3 Sorting buttons**: Fast in-memory qsort by Artist, Title, or BPM (toggles ASC/DESC, preserves active song selection UX).
- [x] Tab 2 — HOT CUES: 8 cue buttons with time positions and authentic Rekordbox green/orange coloring.
- [x] Tab 3 — LOOP: loop in/out controls with 1/2, 1, 2, 4, 8, 16 beat loops.
- [x] Tab 4 — BEAT JUMP: ±1/4/8/16 beat jump buttons.
- [x] Tab 5 — KEY SHIFT: transposition.
- [x] Tab 6 — SETTINGS: configuration, backlight PWM slider (GPIO23), RCA speaker switch, and system/memory diagnostics.


### Hardware Integration — DONE ✅

- [x] Integrated with BSP display (ST7701S MIPI DSI)
- [x] Mapped 800x480 landscape UI onto rotated 480x800 screen using hardware PPA
- [x] Touch input routing through `lv_indev` GT911 driver
- [x] Real deck state polling and asynchronous UI updates
- [x] **Bus Bandwidth & Memory Optimization**: Shifted both Zoom & Static Waveforms to indexed `LV_COLOR_FORMAT_I8` canvas alocated in internal SRAM (~31 KB + ~93 KB). This completely eliminated PSRAM bus saturation and solved the audio underrun / static noise issue while maintaining 60 FPS graphics.

---



## Phase 9: Hardware Integration into CDJ-100S Chassis 🔲

- [ ] Wire CDJ-100S front panel to ESP32-S3 per `firmware/control-board-s3/PINOUT.md`
- [ ] Mount JC4880P443C_I_W display in CDJ-100S opening
- [ ] Build removable wiring loom (ESP32-S3 ↔ CDJ panel)
- [ ] Power: 5V supply, audio line output, safe shutdown
- [ ] Verify GPIO28/29 on JP1 for P4↔S3 UART link

---

## Phase 10: Feature Expansion (post-MVP)

- Full audio playback engine integration (Phase 7)
- Hot cue persistence (flash/NVS)
- Loop roll + beat jump from PQTZ beatgrid
- Key shift / keylock
- FLAC/WAV audio formats
- V2 controls: BTN_LOAD (GPIO21), BROWSE encoder (GPIO17/18)
- MIDI controller mode (use with desktop Mixxx)

---

## Environment Summary

> **Both boards build with ESP-IDF v5.5** (`C:\Espressif\frameworks\esp-idf-v5.5\`).
> The P4 is rev v1.3 (eco2) silicon — it needs `CONFIG_ESP32P4_REV_MIN_FULL=0` in
> `sdkconfig.defaults` (already set) so the bootloader accepts the early-revision chip.

| Tool | Version | Path |
| --- | --- | --- |
| ESP-IDF | v5.5 | `C:\Espressif\frameworks\esp-idf-v5.5\` (venv `idf5.5_py3.11_env`) |
| GCC (PC tests) | 16.1.0 | WinLibs MinGW UCRT (winget) |
| S3 flash port | COM4 | CH343 UART bridge (GPIO43/44) |
| P4 flash port | COM15 | USB-Serial-JTAG |
| S3 MIDI port | COM5 | USB-OTG TinyUSB (GPIO19/20) |

```powershell
# Both targets (S3 and P4)
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1
```
