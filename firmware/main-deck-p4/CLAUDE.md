# CDJ100S-XXX Main Deck Firmware — Claude Guide

## Project Overview

ESP32-P4 firmware for the CDJ100S-XXX main deck board (JC4880P443C_I_W).  
Responsible for: UI (LVGL), deck state machine, audio decode/output, USB media library.  
Communicates with the ESP32-S3 control board via UART1 (7-byte frame protocol).

**Status:** Display, touch, USB media library (FAT32/exFAT on MBR/GPT), audio
(PCM5102A I2S MAIN, MP3/WAV/FLAC), SDMMC mount, display triple buffering, and the
dual-deck P4 touchscreen path are operational on hardware. `deck_core` drives
`audio_engine` through deck-aware APIs; the S3 control board populates the same
event queue over the UART control link (FLX4 controls verified). The Overview
waveform is feature-complete: both decks use the direct PPA overlay path with
"Punchy" colour-waveform rendering, white transient tips, an active/armed loop
region highlight, hot-cue markers on the large + mini waveforms, and a
translucent played-progress highlight on the mini. The **ESP-Hosted Wi-Fi + web
UI mobile controller** is re-enabled behind a Settings switch (default off). A
2026-07-04 audit hardened thread-safety (atomics), load-failure abort, and the
web status JSON. Line-out validation and native FLX4 LED feedback remain the main
pending items.

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
| `app_settings` | ✅ **RUNNING ON HW** | NVS persistence (audio output, backlight %, time mode, cue mode, master trim, `wifi_remote`); apply at boot |
| `ui` | ✅ **RUNNING ON HW** | 4-screen 800×480 dual-deck UI (Overview/Library/Hot Cues/Settings); PPA rotation; touch indev; module-split Overview/Library/Controls/Performance/Settings/Status; Overview waveform loop highlight + hot-cue markers + mini played-progress + per-deck VU meters; 2026-07-09 stability pass (cue-fingerprint guard, tab-return reblit, VU-segment/play-button invalidate diffing, `LV_INV_BUF_SIZE=64`); Settings Wi-Fi remote switch, non-persisted **S3 DEBUG AP** switch (status label OFF/STARTING/ON/ERROR), + "Last reset" diagnostic |
| `bsp_jc4880` | ✅ **RUNNING ON HW** | ST7701 display + GT911 touch + PCM5102A MAIN out (ES8311 dropped); SDMMC `/sd` mount hardware-verified (on-chip LDO ch4; `bsp_sd_init` retries the mount 3× to ride out cold-boot `send_op_cond` timeouts) |
| `audio_engine` | ✅ **RUNNING ON HW** | MP3 (minimp3) + WAV + FLAC (dr_flac) → PCM5102A I2S MAIN + FLX4 USB headphone cue; PSRAM progressive preload; pitch resampling; PVBR/IFI seek on decode task; loop (set/clear/get); dual-deck mixer/EQ/channel-filter/beat-FX (filter/echo/flanger) + Smart CFX; RELAXED-atomic shared state (incl. lock-free deck VU peaks: raw `s_deck_peak` + decaying pre-fader `deck_peak_display`); `ae_fail_load()` aborts a stalled load; SDL2/WAV on PC |
| `wifi_link` | ✅ **RUNNING ON HW** | ESP-Hosted (onboard ESP32-C6, SDIO) SoftAP `PAJONIIR`; Settings toggle (default off); `wifi_link_start/stop` + async `request_enable`; brings up `web_server`/`dns_server` |
| `web_server` | ✅ **RUNNING ON HW** | httpd mobile controller at `http://192.168.4.1`; `/api/status` (dynamic JSON incl. `controller` object), `/api/library`, `/api/control` (play/cue/pfl/volume/crossfader/pitch/loop/seek), `/api/load`; captive DNS |
| `controller_profile_manager` | ✅ **RUNNING ON HW** | Scans `/sd/controllers/<name>/profile.s3bin` at boot (verified 2026-07-09: `profiles:1`), registry + VID/PID match; on S3 descriptor report streams the matched `.s3bin` to the S3 over the 0xA6 bulk layer (sender task, ACK/retry). `CONFIG_CONTROLLER_PROFILE_MANAGER=y`, path `CONFIG_CONTROLLER_PROFILE_SD_PATH=/sd/controllers` |

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

> **Sound is in the default build (2026-07-10).** The FLX4 USB-headphones audio
> profile (PCM5102A RCA MAIN on I2S unit 1 + monitor PCM link on unit 0, ES8311
> off) was folded into `sdkconfig.defaults`, so a plain `idf.py build` now
> produces the audio firmware. The former `sdkconfig.flx4_hp_e2e` /
> `sdkconfig.monitor_link_bench` overlays and all `build_*` dirs were removed.

`idf.py monitor` requires a TTY; for boot logs use pyserial capture
(`serial.Serial('COM15',115200)` + DTR/RTS reset toggle). Keep only one
process active on COM15 (capture and flash are mutually exclusive — "Access is denied").

---

## Architecture

```
S3 (panel events)
    ↓ UART 460800
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
6. `ui_init()` — LVGL 800×480, 4 screens, PPA rotation, touch indev
7. `usb_storage_init(cb)` — USB host; on mount loads library + refreshes UI

> **Wi-Fi remote — user-toggled from Settings, default OFF (2026-07-04).**
> The P4 has no native radio; the onboard **ESP32-C6** provides Wi-Fi over the
> **ESP-Hosted SDIO** transport (slot 1, 4-bit; CLK18/CMD19/D0-D3 14-17; C6
> reset GPIO54). The **Settings tab has a WI-FI REMOTE switch** (default **off**,
> persisted in NVS `app_settings.wifi_remote`). Turning it on runs
> `wifi_link_start()` → `esp_hosted_init()` → `esp_wifi_init()` → SoftAP
> **`PAJONIIR`** (WPA2, pw `12345678`) on **192.168.4.1** + `web_server` +
> captive `dns_server`; turning it off runs `wifi_link_stop()` which tears the
> whole stack back down (`esp_wifi_deinit` + `esp_hosted_deinit`) so the C6/RF
> stops drawing RAM and power. Toggling is **asynchronous** — the UI event only
> calls `wifi_link_request_enable()`, which spawns a worker task so the ~1-2 s
> SDIO/C6 bring-up never blocks the LVGL task. At boot `app_main` re-starts the
> AP only when the saved setting is on. Connect a phone/PC to `PAJONIIR` and
> open `http://192.168.4.1` for the mobile controller (captive portal redirects
> there). Hardware-verified 2026-07-04: default-off, toggle on/off, and reboot
> persistence all work. UI stays decoupled from the transport via
> `ui_settings_set_wifi_toggle_cb()` (registered in `app_main`) to avoid a
> `ui → wifi_link → web_server → ui` component cycle. (Historically parked
> 2026-06-29 for RF-interference-free development; the C6 mempool prefers
> SPIRAM so hosted buffers stay out of scarce internal RAM.)

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
| 0x82 STATE | both | S3→P4 FLX4 connection + S3 Debug AP status; P4→S3 S3 Debug AP enable request (`CTRL_ID_S3_DEBUG_AP` 0x85) |
| 0xA6 BULK | both | variable-length `[A6][type][seq][len][payload][crc16]`: S3→P4 controller descriptor; P4→S3 profile transfer (BEGIN/CHUNK/END/ACTIVATE) + S3→P4 ACK/NACK/STATUS. See `docs/CONTROL_LINK_PROTOCOL.md` |

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

PC unit tests (`tests/anlz/`): via `.\tests\run_p4_host_tests.ps1` → 34/34 PASS

Format details: `docs/rekordbox-format-analysis.md`

---

## Audio Engine (`audio_engine`) ✅ RUNNING ON HARDWARE

minimp3/dr_flac/WAV decode + PCM5102A I2S MAIN output (firmware) / WAV (PC test).
(ES8311 codec was dropped; MAIN out is PCM5102A on I2S_NUM_1, headphone cue is
the FLX4 USB audio path.)

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
| (neither) | ESP32-P4 firmware | PCM5102A MAIN via `i2s_channel_write` (`CONFIG_BSP_PCM5102A_MAIN_OUT`) |

**Firmware Path (running on HW):**
- `bsp_audio_init()` sets up the PCM5102A I2S MAIN channel (I2S_NUM_1);
  `audio_engine_init()` retrieves it via `bsp_audio_get_main_i2s_tx()` and writes
  with `i2s_channel_write` (blocks on I2S DMA → real-time tempo).
- `audio_engine_load()` does not read USB on the caller stack — the **decode task preloads the entire MP3
  into PSRAM**, opens it, and decodes from memory. (Streaming from /usb during
  playback crashes the USB-DWC driver: `usb_dwc_hal.c:502`.)
- minimp3 needs **~26 KB stack** → decode task is 32 KB (NOT on the LVGL/caller stack).
- The output task pitch-resamples/mixes the ring buffer and writes MAIN via `i2s_channel_write()` (blocks on I2S DMA → real-time tempo); headphone cue goes out over the FLX4 USB audio path.
- The PCM5102A I2S clock opens at the sample rate of the first frame (44.1/48k both observed).
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

**FX chain (2026-07-10 DSP pass — "sound better, less touchy on the knobs"):**
per deck the output task runs `EQ → channel filter → pad-FX → beat-FX filter →
beat-FX flanger → beat-FX echo` (`audio_output_mixer.c`). All FX DSP is fixed-
point/integer on the hot path; coefficient/`tanf`/`expf` work is done per block,
not per sample.
- **Channel filter** (`audio_filter.c`) — ZDF (TPT) state-variable filter with a
  mild resonant bump (`AUDIO_FILTER_RES_K` = 1/Q, default 0.8 ≈ +2 dB). One knob:
  low-pass left of centre (**18 kHz → 60 Hz**), high-pass right (**20 Hz → 8 kHz**,
  no dry blend). Cutoff maps **exponentially** so every degree of turn is one
  musical interval; cutoff is smoothed over 32-frame blocks (no zipper). Replaced
  the old cascaded one-pole that never fully killed and bunched all the audible
  change into the last ~20 % of travel.
- **Echo** (`audio_delay_fx.c`) — feedback path has a one-pole **damping LP**
  (~4.5 kHz) so repeats darken per generation (tape-style); wet/feedback gains
  ramp (de-click); **switch-off rings the tail out ~2 s** instead of cutting
  (`audio_delay_fx_is_ringing()`, the output mixer keeps processing while ringing).
  `deck_core` depth→wet uses a sqrt taper (audible early), feedback 0.20–0.68.
- **Flanger** (`audio_flanger_fx.c`, new) — triangle-LFO fractional delay
  **0.6–6 ms** (linear interpolation) with feedback for the resonant jet; the
  BEAT selector sets the LFO period (beat-synced, `beat_fx_flanger_period_ms`).
  Third effect in the `deck_core` cycle: FILTER → ECHO → FLANGER.
- **Smart CFX** (`audio_smart_cfx.c`) — response curve is now a **smoothstep**
  S-curve on the channel-filter knob (fine near the detent, ~1:1 at half turn,
  precise near full kill) instead of the old x² curve that deadened the whole
  first half. Endpoints + a min-audible clamp are preserved.

PC test harness: `tests/audio_engine/` (run all P4 host tests via
`.\tests\run_p4_host_tests.ps1`; `make`/`mingw32-make` is not installed — the
runner compiles each suite with `gcc` from `C:\msys64\ucrt64\bin`).

---

## LVGL UI ✅ RUNNING ON HW

800×480 landscape layout with 4 screens (PPA hw rotation, GT911 touch indev):

| Tab | Screen | Content |
|-----|-------|---------|
| 0 | OVERVIEW | Waveform overview + zoom, playhead, status |
| 1 | LIBRARY | List of tracks from `library_get()` |
| 2 | HOT CUES | 8 cue buttons with time positions |
| 3 | SETTINGS | Configuration |

The dedicated touch **LOOP** and **BEAT JUMP** screens were removed 2026-07-10
(Key Shift went earlier): they duplicated the FLX4 beat-loop / beat-jump pads
(driven through `deck_core`) and the Overview's loop-region + beat markers, so
the touch screens were redundant. The loop/beat-jump **logic** (`deck_core`,
`beat_jump.c`) is unchanged. Removing the screens dropped the tab bar from 6 to
4 (the footer lays tabs out dynamically from `UI_TAB_COUNT`).

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

Overview waveform note: **both decks now use the direct PPA overlay path** for
the large (zoom) waveform on firmware (`ui_overview_scheduler_direct_overlay_allowed`
returns true for deck 0 and 1); the Deck 2 lower-waveform jitter that once forced
an LVGL path was resolved on 2026-06-13. The LVGL I8 `wave_canvas` now exists only
in the PC/simulator build. The scrolling strip cache (`ui_overview_wave_cache`)
renders new columns, burns the fixed centre playhead into the strip, and PPA-blits
segments straight to the framebuffer. The small (mini) full-track waveform is drawn
once at load into an I8 canvas, then progress-tinted as it plays.

**Overview stability pass (2026-07-09)** — removed the periodic waveform hitch and
the VU-driven flash:
- The 1 Hz slow-update no longer resets the wave-cache on every tick. `ui_overview_update_cue_markers`
  keeps an FNV-1a fingerprint of the cue layout + duration and only rebuilds the
  strip when the cues actually change (`s_overview_cue_fingerprint`). This was the
  main source of the ~1 s jitter on both decks.
- The cue-reset used to *also* mask an erase: when LVGL repaints the wave rectangle
  (returning to Overview while paused, or a full-screen redraw) it overwrites the
  direct-PPA waveform. Returning to the Overview tab now re-arms the strip reblit
  (`s_overview_prev_tab`) so the waveform is restored even while paused.
- `ui_overview_update_vu_meter` restyles only the VU segments whose active state
  flips, and the transport play button is restyled only on an actual play/pause
  transition. `LV_INV_BUF_SIZE=64` (global compile def in `CMakeLists.txt`) widens
  LVGL's invalidate buffer so bursts of small invalidations no longer overflow into
  a full-screen redraw (which would erase the PPA waveforms).

**Deck VU meters** — `audio_engine` records a per-deck **pre-fader** peak
(`deck_frame` scaled by pregain × master trim, so it tracks the FLX4 TRIM knob →
`CTRL_ID_CH_TRIM` → pregain; `AE_VU_SENSITIVITY` tunes the reference level) into two
lock-free atomics: `s_deck_peak` (raw, read-and-reset, drained by the FLX4 LED
path) and `s_deck_ui_peak` → snapshot `deck_peak_display` (instant-attack/decaying,
read non-destructively so the on-screen VU never sticks). The Overview VU segments
read `deck_peak_display`; the controller LED VU (`deck_core` `vu_task`, 30 ms) adds
attack/slow-decay ballistics (`vu_ballistic_level`) so the pads don't flicker.

Waveform colours (2026-07-04, "Punchy" scheme): brighter cyan transients
(`#26E0FF`), true-white 2 px tips on loud transients (peak amp ≥ 26), punchier
blue/pink. The RGB565 firmware palette (`s_overview_wave_rgb565_palette`) has
indices 0–9 waveform colours, 10 = dim-amber loop background, and 11–18 = hot-cue
slot colours 0–7; the two LVGL I8 canvas palettes (main WIN32 + mini) mirror
0–9. The sample→colour mapping is `ui_waveform_palette_for_sample()`; the white
tips are `WAVE_TIP_*` in `ui_overview_renderer.c`.

Overview waveform overlays (all baked into the scrolling RGB565 strip so they
PPA-blit atomically with the waveform — no LVGL-over-PPA flicker):
- **Loop region highlight** — amber background over `[loop_start, loop_end)` with
  white edge markers. Active loop is `[in, out]`; an *armed* loop-in (before the
  out is pressed) grows from the marker to the live playhead.
  `deck_core_get_loop_display()` exposes `{active, armed, start, end}`; the region
  is fed to the cache via `ui_overview_wave_cache_set_loop()` (a change forces a
  full strip redraw).
- **Hot-cue markers** — coloured vertical lines + flag heads drawn from
  `meta->cues` (`WAVE_CUE_*`), scrolling with the waveform. The old LVGL cue
  objects are hidden.
- **Mini (full-track) overview** — thin LVGL hot-cue lines across the whole
  track, and a translucent warm-amber "played" overlay (`panel->mini_played`)
  covering `[0, playhead]`. Both sit under the mini playhead. The two decks'
  minis sit side-by-side at the bottom (deck 1 left `info_x=0`, deck 2 right
  `info_x=400`).

**Tap-to-seek.** Both waveforms are tappable per deck (each `*_border` carries
its deck in `user_data`; the canvas/overlays/cue markers/playhead are
non-clickable so taps fall through to the border):
- the **large** waveform (`wave_border` → `waveform_seek_event_cb`) seeks within
  the visible zoom window;
- the **mini** waveform (`mini_wave_border` → `mini_waveform_seek_event_cb`,
  2026-07-10) maps the tap across the **whole track** (`rel_x / OVERVIEW_MINI_CV_W
  × duration`). Active whenever a track is loaded (`duration_ms > 0`), so a tap
  while playing continues from the new point. Both route through
  `ui_overview_config.actions.seek`.

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

`CONFIG_ESP_MAIN_TASK_STACK_SIZE=16384` — `ui_init()` builds all 4 screens on the main task stack;
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
- ✅ ~~**Beat LED** feedback (PQTZ beatgrid → `control_link_send_led`)~~ — dropped 2026-07-10: the beatgrid beat feedback already lives on the Overview screen (`s_beat_pulses` phase strip + red downbeat marker, beatgrid-driven). A controller LED was declined (FLX4 has no dedicated beat LED and hijacking a state LED was not wanted); legacy CDJ-panel GPIO38 BEAT is unused hardware. No hardware LED path needed.
- ✅ ~~WAV/FLAC decode~~ — decoder-abstraction layer (`audio_decoder`/`audio_format`); WAV inline + FLAC via dr_flac over the PSRAM preload; MP3 stays on minimp3
- ✅ ~~`bsp_sd_init()` SDMMC (config/cache)~~ — `/sd` mount hardware-verified
- ✅ ~~Reduce preload-to-play latency on large files (~1-3 s)~~ — P5 progressive preload
- ✅ ~~Tearing optimization of display~~ — triple buffering path implemented and hardware-verified
- ✅ ~~Deck 2 lower Overview waveform jitter~~ — resolved 2026-06-13; both decks now use the direct PPA overlay path
- ✅ ~~ESP-Hosted Wi-Fi + web UI mobile controller~~ — re-enabled 2026-07-04 behind a Settings switch (default off); SoftAP `PAJONIIR` + `http://192.168.4.1`
- ✅ ~~USB-disconnect crash~~ — fixed 2026-07-04 by gating the track-meta-cache USB `stat()` (a disconnect during it panicked the MSC driver + wedged USB)
- ✅ ~~Overview waveform visualisations~~ — "Punchy" colours, loop-region highlight (active + armed), hot-cue markers (large + mini), mini played-progress overlay
- ✅ ~~Overview waveform jitter after VU meters~~ — fixed 2026-07-09: cue-fingerprint guard (no 1 Hz strip reset), tab-return reblit, VU-segment/play-button invalidate diffing, `LV_INV_BUF_SIZE=64`
- ✅ ~~Controller VU meters flicker / ignore TRIM~~ — fixed 2026-07-09: pre-fader peak (pregain × trim) + `AE_VU_SENSITIVITY` + attack/decay ballistics (`vu_ballistic_level`)
- ✅ ~~Hot-cue pad LEDs stale on FLX4 track load~~ — fixed 2026-07-09: load path now republishes the full FLX4 LED snapshot so the loaded deck's hot-cue pads light immediately
- ✅ ~~`/sd` not mounting on cold boot~~ — fixed 2026-07-09: `bsp_sd_init` retries the mount 3× (150 ms) to ride out the transient power-up `send_op_cond` timeout
- ✅ ~~Jog does nothing while playing~~ — fixed 2026-07-10: a jog while playing now does a transient pitch-bend **nudge** for manual beat matching (`audio_engine_deck_jog_nudge` bumps a per-deck `s_jog_bend`; the output task applies `pitch_factor × (1+bend)` and decays it back). Both the platter (`JOG_SCRATCH`) and the ring (`JOG_BEND`) nudge while playing; both scrub the position while paused. The Overview waveform tracks the bend because the mixer snapshot now carries `effective_speed_permille` (fader × bend), fed into the position interpolator instead of the fader-only speed.
- **Line-out (RCA) validation** — hardware measurement of the RCA/monitor output path pending
- ✅ **True scratch / "vinyl mode"** — implemented and hardware-validated 2026-07-11. `JOG_TOUCH` gates platter-top scratch from side-ring bend; a per-deck canonical PSRAM PCM timeline provides retained history plus forward lookahead, bidirectional interpolated playback, click-free release/re-grab, paused/CUE scratch, active-loop wrapping, waveform-head tracking and deferred pitch handoff. Dual-deck stress passed without WDT or monitor PCM drops. **Detailed design and validation record: [`docs/VINYL_SCRATCH_PLAN.md`](../../docs/VINYL_SCRATCH_PLAN.md).**
