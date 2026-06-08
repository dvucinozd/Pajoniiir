# CDJ100S-XXX

Standalone single-deck DJ player built inside a Pioneer CDJ-100S chassis.  
Replaces original internals with two ESP32 boards and a 4.3" touchscreen.

---

## Hardware

| Board | Role |
|-------|------|
| **ESP32-S3-DevKitC-1 N16R8** | CDJ front panel I/O, USB MIDI device, UART link to P4 |
| **JC4880P443C_I_W (ESP32-P4)** | LVGL UI, audio decode/output, USB media library |

**Media:** USB drive with Rekordbox-analyzed tracks (`PIONEER/USBANLZ/` + `PIONEER/rekordbox/export.pdb`).

---

## Firmware

```
firmware/
  control-board-s3/   ESP32-S3 — panel I/O, USB MIDI, control link  ← RUNNING ✅
  main-deck-p4/       ESP32-P4 — deck engine, audio, UI             ← RUNNING & STABILIZED ✅
```

### ESP32-S3 status: **running** ✅

V1 firmware matches original CDJ-100S control layout:
13 buttons (PLAY, CUE, EJECT, TRACK◀/▶, SEARCH◀◀/▶▶, JET, ZIP, WAH, HOLD, TIME/AUTO CUE, MASTER TEMPO),
jog encoder, 14-bit pitch slider, 4 LEDs.  
Heartbeat frame sent to P4 every 5 s so P4 can detect S3 disconnects.

- USB MIDI device: `VID 0x303A / PID 0x4008`, product name `CDJ100S-XXX`
- Flash via COM4 (CH343 UART bridge): `idf.py -p COM4 flash`
- Wiring reference: [`firmware/control-board-s3/PINOUT.md`](firmware/control-board-s3/PINOUT.md)
- Full build guide: [`firmware/control-board-s3/CLAUDE.md`](firmware/control-board-s3/CLAUDE.md)

### ESP32-P4 status: **UI, media library & audio running on hardware** ✅

| Component | Status |
|-----------|--------|
| `control_link` | ✅ Implemented — UART RX/TX, frame parser, S3 heartbeat & feedback |
| `deck_core` | ✅ Implemented — state machine that drives `audio_engine` (play/pause, cue→seek, jog, pitch); `deck_core_queue_event()` accepts UI/S3 control events |
| `library` / `rekordbox_pdb` | ✅ Implemented — PDB parser, track index from export.pdb |
| `library` / `rekordbox_anlz` | ✅ Implemented — ANLZ parser, validated on 308 real tracks |
| `ui` | ✅ Implemented — 7-screen 800×480 layout with real waveform rendering & GT911 Touch |
| `usb_storage` | ✅ Implemented — USB Host MSC → FATFS mount `/usb`, live library load |
| `bsp_jc4880` | ✅ Display (ST7701S MIPI DSI) + touch (GT911) + audio (ES8311/I2S); SDMMC `/sd` config/cache mount verified on JC4880 TF slot |
| `audio_engine` | ✅ Plays on hardware — minimp3 decode → ES8311/I2S, PSRAM preload, pitch resampling. Supports Instant Frame-Index (IFI) Seek with 1-2 ms latency for Hot Cue & Beat Jump |

Build guide: [`firmware/main-deck-p4/CLAUDE.md`](firmware/main-deck-p4/CLAUDE.md)

---

## Inter-Board Link

```
CDJ front panel
      │ (wire harness)
ESP32-S3 ──── UART 115200 baud ────► ESP32-P4
  GPIO40 TX ──────────────────────── GPIO28 RX
  GPIO41 RX ◄─────────────────────── GPIO29 TX

7-byte frame: [0xA5][type][id][val_lo][val_hi][seq][checksum]
```

---

## Rekordbox Media Library

The P4 reads two file types from the USB drive at startup:

### Pioneer Hardware Database (`export.pdb`)

`PIONEER/rekordbox/export.pdb` — not encrypted, contains the full track list.

| Field | Source | Used by |
|-------|--------|---------|
| `file_path` | PDB Tracks table | library — audio file path |
| `anlz_path` | PDB Tracks table | library — **direct path to ANLZ0000.DAT** |
| `title`, `artist`, `album` | PDB Tracks + name tables | UI — track browser |
| `bpm` (coarse) | PDB tempo field | deck_core — initial display |

Key insight: the PDB stores the ANLZ path directly per track — no directory walking required.  
308/308 tracks on the test drive have valid ANLZ paths.

### ANLZ Files (`ANLZ0000.DAT` / `.EXT`)

`PIONEER/USBANLZ/<hash>/<hash>/ANLZ0000.DAT` — loaded on-demand per track.

| Tag | Content | Used by |
|-----|---------|---------|
| PPTH | Audio file path (UTF-16 BE) | library — path verification |
| PVBR | 400× VBR seek offsets | audio_engine — fast MP3 seek |
| PQTZ | Beat grid + precise BPM | deck_core — BPM display, beat sync |
| PWAV | 400B low-res waveform | UI — waveform strip |
| PCOB | Hot cues + loops (up to 8) | deck_core — cue points |
| PWV3 (EXT) | Up to ~62 KB high-res waveform | UI — zoom waveform |

Tested on real Rekordbox USB drive (308 tracks, GCC 16.1.0, 32 ANLZ unit tests pass).

Test harnesses: [`tests/anlz/`](tests/anlz/) · [`tests/rekordbox_pdb/`](tests/rekordbox_pdb/)

Format details: [`docs/rekordbox-format-analysis.md`](docs/rekordbox-format-analysis.md)

---

## Performance & Stability Optimizations (Heavy Load Tuning)

When tested under heavy load with a real USB drive containing **308 tracks**, the ESP32-P4 was fully stabilized against core panics and watchdog resets via key optimizations:

1. **Pointer-Based Library Engine**:
   Replaced stack-heavy `library_get(index, out_struct)` (2.9 KB track copying per row) with `library_get_ptr(index)`, avoiding memory fragmentation and stack overflows during rapid table population.
2. **PSRAM Instruction & Rodata Cache**:
   Configured ESP32-P4 MMU to fetch instructions and read-only data directly from 32MB PSRAM (`CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y`, `CONFIG_SPIRAM_RODATA=y`), maximizing speed and freeing internal SRAM.
3. **Internal SRAM Safeguarding**:
   Established `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=256` to direct all allocations over 256 bytes (like UI structures) to PSRAM, leaving SRAM open for system threads. Enforced standard `malloc` in LVGL (`CONFIG_LV_USE_CLIB_MALLOC=y`).
4. **Instant Seek & Memory-Mapped Audio Engine (Hot Cue & Beat Jump)**:
   Replaced buggy `fmemopen` memory stream emulation with direct memory-mapped access (`file_buf` / `file_pos`) to bypass newlib's faulty buffer synchronization and fseek/ftell lockups. Introduced **Instant Frame-Index (IFI) Seek** which parses MP3 headers at load-time in **23 ms** (for a 7.7MB file) to build a frame seek table in PSRAM. This enables O(1) frame lookup, achieving a latency of **1-2 ms** for Hot Cues and Beat Jumps with zero audio pops or UI lag.
5. **Results**:
   The 308-track library is loaded and rendered in **261 ms** with **>400 KB internal SRAM** and **27.3 MB PSRAM** remaining free. Audio seek and Hot Cue responses on the hardware are instantaneous (under 2 ms) and stable.

---

## Build Environment

- **ESP-IDF v5.5** at `C:\Espressif\frameworks\esp-idf-v5.5\`
- **Python venv**: `C:\Espressif\python_env\idf5.5_py3.11_env`
- **Native GCC 16.1.0** (WinLibs MinGW UCRT) — for PC unit tests

```powershell
# ESP-IDF environment (run once per shell)
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1

# Flash S3 (COM4)
cd firmware/control-board-s3
idf.py -p COM4 flash monitor

# Build & Flash P4 (COM15)
cd firmware/main-deck-p4
idf.py -p COM15 build flash monitor

# Run ANLZ unit tests (PC)
cd tests/anlz
mingw32-make test

# Run PDB unit tests (PC) + real USB drive
cd tests/rekordbox_pdb
mingw32-make test
.\test_pdb.exe F:\PIONEER\rekordbox\export.pdb
```

---

## Documentation

See [`docs/`](docs/) for architecture decisions, board analysis, development plan, and wiring map.

Local-only reference folders (not in git — large vendor/upstream inputs):

- `JC4880P443C_I_W/` — board schematics, examples, user manual
- `upstream/XDJ100SX/` — original project this is based on
- `upstream/esp32_p4_jc4880p433c_bsp/` — ESP-IDF BSP candidate for P4 display/touch
