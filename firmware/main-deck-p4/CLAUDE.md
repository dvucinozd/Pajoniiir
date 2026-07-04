# CDJ100S-XXX Main Deck Firmware — Claude Guide

## Project Overview

ESP32-P4 firmware for the CDJ100S-XXX main deck board (JC4880P443C_I_W).  
Responsible for: UI (LVGL), deck state machine, audio decode/output, USB media library.  
Communicates with the ESP32-S3 control board via UART1 (7-byte frame protocol).

**Status:** Display, touch, USB media library, audio (ES8311 + I2S), SDMMC mount,
display triple buffering, and the dual-deck P4 touchscreen path are operational
on hardware. `deck_core` drives `audio_engine` through deck-aware APIs for local
touchscreen control; the S3 will populate the same event queue after FLX4 raw
MIDI capture is proven. The P4 UI has been refactored into focused modules, and
the 2026-06-13 Deck 2 lower Overview waveform jitter fix is in `master`.
Line-out validation, native FLX4 input, native FLX4 LED feedback, and the cue/PFL
audio output path remain pending.

---

## Components

| Component | Status | Description |
|-----------|--------|-------------|
| `control_link` | ✅ **IMPLEMENTED** | UART RX/TX, frame parser, LED send |
| `deck_core` | ✅ **RUNNING ON HW** | state machine + drives `audio_engine` (play/pause/cue→seek/jog/pitch); `deck_core_queue_event()` for UI/S3 sources |
| `library/rekordbox_pdb` | ✅ **RUNNING ON HW** | PDB parser — title/artist/album/anlz_path + musical key (Keys table 0x05) from export.pdb |
| `library/rekordbox_anlz` | ✅ **RUNNING ON HW** | ANLZ parser — BPM/beatgrid/waveform/cues; section-header tag walk; 34 unit tests |
| `library` (USB) | ✅ **RUNNING ON HW** | `library_init()` loads the track index from `/usb`; publish-on-write sort; `library_get_summary()` copy accessor (`library_get_ptr()` is simulator-only) |
| `usb_storage` | ✅ **RUNNING ON HW** | USB Host + MSC → `usb_media_mount` (FAT32/exFAT on superfloppy, MBR, or GPT) → `/usb`; callback reloads library + UI |
| `app_settings` | ✅ **RUNNING ON HW** | NVS persistence (audio output, backlight %, time mode); apply at boot |
| `ui` | ✅ **RUNNING ON HW** | 7-screen 800×480 dual-deck UI; PPA rotation; touch indev; module-split Overview/Library/Controls/Performance/Settings/Status; `ui_trigger_library_refresh()` |
| `bsp_jc4880` | ✅ **RUNNING ON HW** | ST7701 display + GT911 touch + PCM5102A MAIN out (ES8311 dropped); SDMMC `/sd` mount hardware-verified |
| `audio_engine` | ✅ **RUNNING ON HW** | MP3 (minimp3) + WAV + FLAC (dr_flac) → PCM5102A I2S MAIN + FLX4 USB headphone cue; PSRAM progressive preload (direct buffer, no fmemopen); pitch resampling; PVBR/IFI seek on decode task; loop (set/clear/get); dual-deck mixer/EQ/filter/beat-FX; SDL2/WAV on PC |

---

## Build & Flash

**IDF path**: `C:\Espressif\frameworks\esp-idf-v5.5\` | **Python venv**: `idf5.5_py3.11_env`

> ⚠️ **P4 is rev v1.3 (eco2) silicon.** Works with IDF 5.5 ONLY with
> `CONFIG_ESP32P4_REV_MIN_FULL=0` in `sdkconfig.defaults` (otherwise the bootloader rejects
> the early-revision chip). This setting is already configured. (The earlier assumption that
> 5.4.1 was required is no longer valid — 5.5 works with this config.)

```powershell
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1
cd firmware/main-deck-p4
idf.py build
idf.py -p COM15 flash               # device is on COM15
```

`idf.py monitor` requires a TTY; for boot logs use pyserial capture
(`serial.Serial('COM15',115200)` + DTR/RTS reset toggle). Keep only one
process active on COM15 (capture and flash are mutually exclusive — "Access is denied").

---

## Architecture

```
S3 (panel events)
    ↓ UART 115200
control_link  →  ctrl_event_queue  →  deck_core
                                           ↓
                               audio_engine  |  ui (LVGL)
                                           ↓
                           control_link_send_led()  →  S3 LEDs
```

**Initialization sequence** (from `app_main.c`):
1. `deck_core_init()` — creates ctrl_event_queue
2. `control_link_init(queue)` — UART RX task, pushes events onto queue
3. `bsp_display_init()` ✅ + `bsp_touch_init()` ✅ + `bsp_audio_init()` ✅ + `bsp_sd_init()` (`/sd`, non-fatal without card)
4. `library_init()` — returns NOT_FOUND until USB is mounted (OK at boot)
5. `audio_engine_init()` — grabs the PCM5102A/monitor output handles, creates mutex/tasks
6. `ui_init()` — LVGL 800×480, 7 screens, PPA rotation, touch indev
7. `usb_storage_init(cb)` — USB host; on mount loads library + refreshes UI

> **Boot log note — `wifi_link_init(host): ESP_ERR_NOT_SUPPORTED` is expected.**
> The P4 has no native Wi-Fi; the web AP path needs an ESP-Hosted co-processor,
> which is disabled. `wifi_link_init()` returns `ESP_ERR_NOT_SUPPORTED` by
> design and `app_main` skips `web_server_start()`/`dns_server_start()`. This
> is not a fault — the touchscreen UI is the primary surface.

---

## UART Control Link Protocol

7-byte frame (same on S3 and P4 sides):
```
[0xA5] [type] [id] [val_lo] [val_hi] [seq] [checksum]
checksum = type ^ id ^ val_lo ^ val_hi ^ seq
```

| Type | Direction | Meaning |
|------|-------|----------|
| 0x01 BUTTON | S3→P4 | id=button_id (0–13), val=0/1 |
| 0x02 ENCODER | S3→P4 | id=0 (jog) / id=1 (browse), val=signed delta |
| 0x03 PITCH | S3→P4 | id=0, val=0–16383 (14-bit) |
| 0x04 HEARTBEAT | S3→P4 | id=0, val=uptime s |
| 0x81 LED | P4→S3 | id=led_id (0–3), val=0/1/2 (off/on/blink) |

---

## UART Pins (P4 side, JP1 header)

| Signal | GPIO | Note |
|--------|------|----------|
| UART1 RX | **GPIO28** | receives from S3 GPIO40 TX (JP1 pin 19) |
| UART1 TX | **GPIO29** | sends to S3 GPIO41 RX (JP1 pin 12) |

**Hardware verification required** — GPIO28/29 are JP1 pins 19/12.

---

## Rekordbox Media Library

### Pioneer Hardware Database (`export.pdb`) ✅

Implemented. `library_init()` calls `pdb_open("/usb/PIONEER/rekordbox/export.pdb")`.

```c
// PDB contains for each track:
pdb_track_t t;
// t.title, t.artist, t.album
// t.file_path   — /Contents/...
// t.anlz_path   — /PIONEER/USBANLZ/.../ANLZ0000.DAT  ← direct, no directory walker needed
// t.bpm, t.duration_s, t.track_id
```

Key finding: PDB contains the **direct ANLZ path** — `anlz_walk_usbanlz()` is not required.  
308/308 tracks on the test USB drive have a valid ANLZ path.

PC test harness: `tests/rekordbox_pdb/`

### ANLZ parser (`rekordbox_anlz`) ✅

`library_load_anlz()` uses `track->anlz_path` directly.

```c
// Basic usage:
anlz_metadata_t meta;
esp_err_t rc = anlz_parse_dat("/usb/PIONEER/USBANLZ/P000/00000832/ANLZ0000.DAT", &meta);
if (rc == ESP_OK) {
    anlz_parse_ext(".../ANLZ0000.EXT", &meta);  // high-res waveform (optional)
    // meta.bpm, meta.beats, meta.cues, meta.waveform_low, meta.waveform_high
    anlz_free(&meta);
}
```

PC unit tests (`tests/anlz/`): `mingw32-make test` → 32/32 PASS

Format details: `docs/rekordbox-format-analysis.md`

---

## Audio Engine (`audio_engine`) ✅ RUNNING ON HARDWARE

minimp3 single-header decode + ES8311/I2S output (firmware) / WAV (PC test).

```c
// Typical usage:
audio_engine_init();
audio_engine_load(mp3_path, track->has_pvbr ? track->pvbr : NULL, track->duration_ms);
audio_engine_play();
audio_engine_set_pitch(raw_pitch);   // 0=+10%, 8192=±0%, 16383=-10%
uint32_t pos = audio_engine_position_ms();
audio_engine_seek(30000);            // seek to 30 s
audio_engine_pause();
audio_engine_stop();
```

**Platform compile-time defines:**

| Define | Platform | Audio output |
|--------|-----------|-------------|
| `AUDIO_ENGINE_PC_TEST` | PC offline test | `audio_engine_decode_to_wav()` → WAV file |
| (neither) | ESP32-P4 firmware | ES8311/I2S via `esp_codec_dev` |

**Firmware Path (running on HW):**
- `bsp_audio_init()` creates the ES8311 codec dev; `audio_engine_init()` retrieves it.
- `audio_engine_load()` does not read USB on the caller stack — the **decode task preloads the entire MP3
  into PSRAM**, opens it, and decodes from memory. (Streaming from /usb during
  playback crashes the USB-DWC driver: `usb_dwc_hal.c:502`.)
- minimp3 needs **~26 KB stack** → decode task is 32 KB (NOT on the LVGL/caller stack).
- The output task pitch-resamples the ring buffer and writes via `esp_codec_dev_write()` (blocks on I2S DMA → real-time tempo).
- Codec opens at the sample rate of the first frame (44.1/48k both observed).
- **Control flows through `deck_core`**: UI "LOAD TRACK" → `audio_engine_load`; touch PLAY/CUE/loop/
  hot-cue/beat-jump → `deck_core`/`audio_engine`. `audio_engine_seek()` only sets
  `seek_target_ms` — the actual seek (PVBR O(1) or linear) is handled by the **decode task** (32 KB stack).
- **Loop:** `audio_engine_set_loop/clear_loop/get_loop_state`; output/decode loop performs auto-seek
  to `loop_start` when position reaches `loop_end`.

**PVBR Seek:** `library_track_t.pvbr[400]` populated by `library_load_anlz()` from ANLZ.  
Seek O(1) when PVBR present; linear scan from start when absent.
(Note: `seek_linear` uses a large stack — take care when connecting seek to S3 controls.)

**Pitch Resampling:** linear interpolation (SDL callback on PC / output task on HW).  
`factor = 1.0 + (8192 − raw_pitch) / 8192.0 × 0.10`

PC test harness: `tests/audio_engine/`  
```
cd tests/audio_engine
mingw32-make test                         # synthetic tests (11/11 PASS)
mingw32-make test MP3=F:/Contents/...mp3  # real MP3 decode to out.wav
```

---

## LVGL UI ✅ RUNNING ON HW

800×480 landscape layout with 7 screens (PPA hw rotation, GT911 touch indev):

| Tab | Screen | Content |
|-----|-------|---------|
| 0 | OVERVIEW | Waveform overview + zoom, playhead, status |
| 1 | LIBRARY | List of tracks from `library_get()` |
| 2 | HOT CUES | 8 cue buttons with time positions |
| 3 | LOOP | Loop in/out controls |
| 4 | BEAT JUMP | Beat jump buttons (±1, ±2, ±4, ±8 beats) |
| 5 | KEY SHIFT | Transposition by semitones |
| 6 | SETTINGS | Configuration |

Header (always visible): title, artist (left), status indicator, two time counters
in the middle, BPM + pitch% (aligned to the right edge).

`ui.c` is now the top-level orchestrator (init, screen registry, tab switching,
frame-context construction) and is intentionally kept under 1000 lines. Screen
and backend ownership has moved into focused modules:

- `ui_lvgl_backend`: display/touch/LVGL task/PPA flush plumbing.
- `ui_overview`: dual-deck Overview screen construction and update flow.
- `ui_library`: track table, selection, source labels, and D1/D2 load buttons.
- `ui_controls` and `ui_performance_tabs`: transport/performance controls.
- `ui_settings`: Settings screen and system/mixer status.
- `ui_status`: header, status/time/BPM/pitch labels, and legacy active-target LED output.
- `ui_overview_renderer`, `ui_overview_scheduler`, `ui_waveform_model`, and
  `ui_position_interpolator`: testable waveform helpers.

Overview waveform note: Deck 1 may use the direct PPA overlay path. Deck 2 uses
the normal LVGL invalidation/flush path because the direct overlay path caused
visible lower waveform jitter on hardware; this was fixed and visually verified
on 2026-06-13.

**Header Time Counters:**
- Left (larger, blue) = elapsed time (current position).
- Right next to it (smaller) = remaining time until the end of track (`-MM:SS.cc`).
- Color warning: gray → **yellow ≤30 s** → **red ≤10 s** to the end (only while track is
  loaded; idle remains neutral gray). The old click-toggle elapsed/remaining
  was removed as both are now displayed simultaneously.

**Colors (`ui_theme.h`):** chrome palette (backgrounds, borders, text, accents) is
centralized in `components/ui/ui_theme.h` as `COL_*` tokens. Feature-specific
colors (waveform, playhead, cue pads, status amber/red, beat-pulse) remain inline.

> ⚠️ **LVGL `%f` does not work.** LVGL builtin `vsnprintf` has float support only with
> `LV_USE_FLOAT` enabled (which would also switch `lv_value_precise_t` to float across all transform/anim math — not desired).
> Therefore, `lv_label_set_text_fmt(..., "%.2f", ...)` yields an **empty string**. Decimal numbers
> (BPM, pitch) are formatted via integer math (`ui_label_set_f2()` and `"%d.%02d"`). Do not use `%f` in LVGL formats.

---

## Display (ST7701 MIPI-DSI) — `bsp_jc4880.c` + `ui_lvgl_backend.c` ✅ RUNNING ON HARDWARE

Panel: ST7701S, **480×800 native portrait**, MIPI-DSI 2 lane @ 500 Mbps, RGB565.
UI is 800×480 landscape → rotated 90° in **hardware via PPA** (not LVGL sw-rotation).

**Key Config (all from vendor demo — see reference below):**
- DPI: 34 MHz pixel clock, RGB565 (`pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565`, `bits_per_pixel = 16`)
- Timing: HSYNC=12 HBP=42 HFP=42, VSYNC=2 VBP=8 VFP=166
- **ST7701 init sequence is panel-specific** (`s_st7701_init_cmds[]` in `bsp_jc4880.c`).
  Component default init (init_cmds=NULL) does NOT work → black screen. Requires vendor sequence.
- `flags.use_mipi_interface = 1` (otherwise the driver falls back to RGB path)
- Backlight GPIO23 (simple GPIO high = on; vendor uses LEDC PWM), reset GPIO5

**Rotation (PPA):** `ui_lvgl_backend.c` creates the LVGL display as 800×480 landscape (WITHOUT `lv_display_set_rotation`),
the flush callback performs `ppa_do_scale_rotate_mirror()` (angle `PPA_SRM_ROTATION_ANGLE_270`) from the LVGL
render buffer into the inactive DPI frame buffer (480×800), then requests a swap on the refresh boundary.
FULL render mode, 2 PSRAM render buffers (cache-aligned) + 3 DPI frame buffers.
`esp_cache_msync(C2M)` on the render buffer before PPA because PPA DMA reads from PSRAM.

**Why NOT LVGL sw-rotation:** PARTIAL mode + rotation → corrupts memory, crashes the DSI GDMA ISR.
FULL mode + `set_rotation` → does not rotate at all. Thus hardware PPA is used.

**Why NOT DMA2D in DPI:** `use_dma2d=true` + LVGL flush from DRAM buffer → `esp_cache_msync` on
non-PSRAM address fails ("invalid addr"), DSI hangs. (Now we write directly to FB via PPA anyway.)

**Vendor Reference:** `C:\Users\Daniel\Desktop\ESP-BOARDS\JC4880P443C_I_W\1-Demo\idf_examples\ESP-IDF\`
— `lvgl_sw_rotation/main/` (init cmds, DPI timing, PPA rotation in `lvgl_port_v9.c`),
`lvgl_demo_v9` (bsp). Contains schematic, datasheet (`4-Driver_IC_Data_Sheet`), and user manual.

### Touch (GT911) ✅ WORKS
- GT911 on **shared I2C bus** (I2C_NUM_1, SDA=GPIO7, SCL=GPIO8), addr 0x5D, reset/INT = NC.
  The same bus will be shared by the ES8311 codec (`bsp_get_i2c_bus()`).
- Coordinate transform for our 90° rotation: **swap_xy=1, mirror_x=1** (x_max=480, y_max=800 native).
  Taps map accurately (verified). Values from vendor demo ROTATION_90.
- `ui_lvgl_backend.c` registers the LVGL pointer indev which polls `esp_lcd_touch_get_coordinates()`.
- The I2C pull-up warning in logs is benign (the board has external pull-ups; GT911 reads ID OK).

---

## SPIRAM

`CONFIG_SPIRAM=y` + `CONFIG_SPIRAM_USE_MALLOC=y` + `CONFIG_SPIRAM_MODE_HEX=y` in `sdkconfig.defaults`.

**`CONFIG_SPIRAM_SPEED_200M=y` + `CONFIG_IDF_EXPERIMENTAL_FEATURES=y` are BOTH REQUIRED** for the display:
on default 20 MHz PSRAM, the DSI cannot fetch the frame buffer in time → "underrun" on every refresh.
(32 MB HEX/16-line PSRAM on this board.)

Also required because the PDB track index (~281 KB) and ANLZ waveforms do not fit in DRAM.  
`library.c` uses `heap_caps_malloc(MALLOC_CAP_SPIRAM)` — falls back to internal heap if SPIRAM is unavailable.

`CONFIG_ESP_MAIN_TASK_STACK_SIZE=16384` — `ui_init()` builds all 7 screens on the main task stack;
default 3.5 KB overflows (stack-protection panic).

---

## Board Pin Reference (JC4880P443C_I_W)

| Function | GPIO |
|----------|------|
| LCD Backlight | GPIO23 |
| LCD Reset | GPIO5 |
| Touch I2C SDA/SCL | GPIO7/8 (shared with codec) |
| Codec I2C addr | 0x18 |
| Touch I2C addr | 0x5D |
| I2S MCLK/BCLK/LRCK | GPIO13/12/10 |
| I2S DIN/DOUT | GPIO48/9 |
| Speaker PA | GPIO11 |
| SDMMC D0-D3/CMD/CLK | GPIO39–42/44/43 |
| JP1 Free GPIOs | GPIO28–35, GPIO49–52 |

**Do not use**: GPIO5/7/8/9/10/11/12/13/23/39–44/48

---

## Music Medium

**USB Drive** (not SD card!). Rekordbox-formatted USB.  
Metadata: `PIONEER/rekordbox/export.pdb` (track index) + `PIONEER/USBANLZ/` (ANLZ files).  
Audio files in `Contents/` or custom folders.

SD card (GPIO39–44) reserved for config/cache.

Format details: `docs/rekordbox-format-analysis.md`

---

## Pending

- ✅ ~~BSP display (ST7701)~~ — WORKS (verified, COM15)
- ✅ ~~BSP touch (GT911)~~ — WORKS (taps accurate)
- ✅ ~~BSP audio~~ — WORKS (PCM5102A MAIN out; MP3 playback 44.1/48k; ES8311 dropped)
- ✅ ~~USB host + VFS mount `/usb`~~ — WORKS (FAT32/exFAT on MBR/GPT via `usb_media_mount`, library auto-load)
- ✅ ~~GPIO28/29 UART link~~ — verified end-to-end with S3
- ✅ ~~S3 DDJ-FLX4 raw MIDI capture~~ — FLX4 enumerates on hardware; translator maps the MVP + extended controls to control-link frames
- **S3 deck_core → audio_engine control** (play/pause/cue/jog/pitch/seek from S3) — mapped; full hardware pass pending
- **Beat LED** feedback (PQTZ beatgrid → `control_link_send_led`)
- ✅ ~~WAV/FLAC decode~~ — decoder-abstraction layer (`audio_decoder`/`audio_format`); WAV inline + FLAC via dr_flac over the PSRAM preload; MP3 stays on minimp3
- ✅ ~~`bsp_sd_init()` SDMMC (config/cache)~~ — `/sd` mount hardware-verified
- ✅ ~~Reduce preload-to-play latency on large files (~1-3 s)~~ — P5 progressive preload
- ✅ ~~Tearing optimization of display~~ — triple buffering path implemented and hardware-verified
- ✅ ~~Deck 2 lower Overview waveform jitter~~ — fixed by disabling direct overlay for Deck 2
