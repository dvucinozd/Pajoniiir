# CDJ100S-XXX Control Board Firmware — Claude Guide

## Project Overview

ESP32-S3-DevKitC-1 N16R8 firmware for CDJ100S-XXX control board.  
Handles CDJ-100S front panel I/O, USB MIDI device, and UART control link to ESP32-P4.

**Status: S3/P4 compatible** ✅ — V1 controls plus LOAD/BROWSE are implemented. Front panel wiring still needs hardware smoke tests.

---

## Control Surface

### Implemented

| Control | GPIO | MIDI |
|----------|------|------|
| PLAY/PAUSE | GPIO8 | Ch1 Note 60 |
| CUE | GPIO7 | Ch1 Note 61 |
| EJECT | GPIO2 | Ch1 Note 63 |
| TRACK SEARCH ◀ | GPIO3 | Ch1 Note 64 |
| TRACK SEARCH ▶ | GPIO4 | Ch1 Note 65 |
| SEARCH ◀◀ | GPIO5 | Ch1 Note 66 |
| SEARCH ▶▶ | GPIO6 | Ch1 Note 67 |
| JET (Perf1) | GPIO9 | Ch1 Note 68 |
| ZIP (Perf2) | GPIO10 | Ch1 Note 69 |
| WAH (Perf3) | GPIO11 | Ch1 Note 70 |
| HOLD (Perf4) | GPIO12 | Ch1 Note 71 |
| TIME MODE/AUTO CUE | GPIO13 | Ch1 Note 72 |
| MASTER TEMPO | GPIO14 | Ch1 Note 62 |
| LOAD | GPIO21 | Ch1 Note 73 |
| Jog dial A/B | GPIO15/16 | Ch2 CC20 (CW=65, CCW=63) |
| Browse A/B | GPIO17/18 | Ch3 Notes 70/71 |
| Pitch slider | GPIO1 (ADC1 CH0) | Ch1 CC0+CC32 (14-bit) |

LEDs: CUE=GPIO33, PLAY=GPIO34, BEAT=GPIO38, END=GPIO39 (active-high, 220Ω)

---

## Build & Flash

**Always flash via COM4 (CH343 UART bridge). Never COM5.**  
COM5 = USB-OTG TinyUSB MIDI device (becomes VID_303A:PID_4008 after flashing).

```powershell
# Prepare environment (once per shell) — same IDF as P4
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1Document

# Flash and monitor logs
cd firmware/control-board-s3
idf.py -p COM4 flash monitor

# Build only (no flash)
idf.py build
```

**ESP-IDF**: v5.5 | **Target**: `esp32s3` | **IDF path**: `C:\Espressif\frameworks\esp-idf-v5.5\`  
**Venv**: `idf5.5_py3.11_env`

---

## Critical sdkconfig Settings

Must be in `sdkconfig.defaults`. Check if CMake regenerates `sdkconfig`.

| Key | Value | Why |
|-----|-------|-------|
| `CONFIG_TINYUSB_MIDI_COUNT` | `1` | Enables MIDI driver (default is 0 → linker error) |
| `CONFIG_ESP_CONSOLE_UART_DEFAULT` | `y` | Console on UART0/COM4 |
| `CONFIG_ESP_CONSOLE_SECONDARY_NONE` | `y` | **CRITICAL**: USB-JTAG secondary conflicts with TinyUSB |
| `CONFIG_TINYUSB_MODE_SLAVE` | `y` | DMA mode causes enumeration failure |
| `CONFIG_FREERTOS_HZ` | `1000` | 1ms tick for debounce timers |

If MIDI stops working: first check `CONFIG_TINYUSB_MIDI_COUNT`.

---

## Architecture

```
panel_io  →  panel_event_t queue
                   ↓
               router_task
              /           \
      midi_compat      control_link
    (TinyUSB MIDI)   (UART1 → P4)
```

### Components

| Component | Description |
|-----------|------|
| `panel_io` | Buttons (active-low, pull-up), PCNT encoders, ADC pitch, LED output |
| `midi_compat` | TinyUSB MIDI device, maps buttons/encoders/pitch to MIDI |
| `control_link` | UART1 binary protocol to ESP32-P4 |
| `calibration` | Pitch fader center/deadzone/invert (GPIO1 ADC1 CH0) |

---

## Dual USB Architecture

ESP32-S3 has two USB peripherals on the same GPIO19/20:
1. **USB-Serial/JTAG** (built-in, default) → formerly COM5
2. **USB-OTG** (TinyUSB) → becomes MIDI device after flashing

Mutually exclusive. `CONFIG_ESP_CONSOLE_SECONDARY_NONE=y` prevents conflicts.  
After first flash: **physically replug the USB-OTG cable** for enumeration in Windows.

---

## UART Control Link Protocol

```
[0xA5][type][id][val_lo][val_hi][seq][checksum]
checksum = type ^ id ^ val_lo ^ val_hi ^ seq
```

| Type | Direction | Content |
|------|-------|---------|
| 0x01 BUTTON | S3→P4 | id=button_id (0–13), val=0/1 |
| 0x02 ENCODER | S3→P4 | id=0 (jog) / id=1 (browse), val=signed delta |
| 0x03 PITCH | S3→P4 | id=0, val_lo+val_hi = 14-bit pitch |
| 0x04 HEARTBEAT | S3→P4 | id=0, val=uptime seconds |
| 0x81 LED | P4→S3 | id=led_id (0–3), val=0/1/2 (off/on/blink) |

UART1: TX=GPIO40 → P4 GPIO28, RX=GPIO41 ← P4 GPIO29, 115200 baud.

---

## PCNT Encoders

- IDF 5.5 API: `pcnt_channel_handle_t` (not old `pcnt_unit_t`)
- X4 quadrature: two channels per unit, `accum_count=true`
- Glitch filter: 1µs
- JOG: GPIO15(A)/GPIO16(B)
- BROWSE: GPIO17(A)/GPIO18(B)

---

## Pending

- **Wire CDJ-100S front panel** according to `PINOUT.md` (physical hardware task)
- Hardware acceptance: confirm LOAD/BROWSE, pitch direction, LED feedback, and offline heartbeat timeout on a physical S3/P4 pair
