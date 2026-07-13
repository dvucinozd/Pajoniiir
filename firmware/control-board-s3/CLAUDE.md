# DDJ-FFL4 S3 Control Board Firmware - Claude Guide

Documentation status: current developer guide, audited 2026-07-13. The
hardware-accepted unsigned OTA baseline is `RC1-106-g717b6ab3`,
`ota_0 / valid`. Signed `.ddjota` verification is implemented and awaits
hardware acceptance.

## Project Overview

ESP32-S3-DevKitC-1 N16R8 firmware for the DDJ-FFL4 control board role.
The active target is a Pioneer DDJ-FLX4 USB MIDI host and translator feeding
deck-aware `0xA5` UART control-link frames to the ESP32-P4.

The inherited CDJ-100S GPIO panel path remains available as a compatibility
mode when `CONFIG_DDJ_FLX4_HOST_MODE` is disabled.

Status:

- default build (`sdkconfig.defaults`): DDJ-FLX4 MIDI-to-P4 translator
  (`CONFIG_DDJ_FLX4_TRANSLATE_TO_P4=y`) **plus the FLX4 USB-headphone audio
  path** (`CONFIG_DDJ_FLX4_USB_AUDIO_HEADPHONES` + `CONFIG_P4_AUDIO_LINK_ENABLED`,
  folded into defaults on 2026-07-10 — a plain `idf.py build` now has sound);
- to fall back to the raw MIDI capture logger, disable
  `CONFIG_DDJ_FLX4_TRANSLATE_TO_P4` in menuconfig (or `CONFIG_DDJ_FLX4_HOST_MODE`
  for the inherited CDJ panel USB-MIDI device mode);
- FLX4 enumerates and translates to control-link frames on hardware
  (VID 0x2B73 / PID 0x0045); the XIAO GPIO21 user LED reflects reduced link
  state.

S3 web OTA accepts only signed `control-board-s3.ddjota` bundles. The shared
`ota_manifest` component verifies the ECDSA P-256 manifest before flash erase
and the streamed image SHA-256 before activation. Firmware embeds only the
committed public DER key; the repository-root ignored private PEM is release
infrastructure and must never be committed or copied onto the device.

Boot note: the FLX4 briefly disconnects/re-enumerates ~0.9 s into boot on
every cold start (USB host settling). It self-recovers within ~0.6 s — the
connection state is republished and no packets are lost. Benign; not a fault.

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
— legacy CDJ panel path only; in FLX4 host mode `panel_io` is not initialised
and LED frames from the P4 go to the FLX4 over USB MIDI instead.

Onboard XIAO user status LED (`status_led` component, FLX4 host modes):
GPIO21 active-low. It provides reduced one-LED feedback: on while the P4 link is
down or FLX4 is connected, off when P4 is up and FLX4 is disconnected, with a
short activity tick on MIDI input.

---

## Build & Flash

Use the CH343 UART bridge serial port for flashing and logs. Do not use the
USB-OTG port as a serial console while testing FLX4 host mode; GPIO19/20 are
owned by the USB host/device stack.

```powershell
# Prepare environment (once per shell) - same IDF as P4
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1

# Flash and monitor logs
# NOTE: the CH343 COM number can move between replugs (seen as COM4, now COM3);
# if the port is missing, list ports and pick the "USB-Enhanced-SERIAL CH343" entry.
cd firmware/control-board-s3
idf.py -p COM3 flash monitor

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
| `CONFIG_DDJ_FLX4_HOST_MODE` | `y` | Default DDJ-FLX4 raw USB MIDI capture mode |
| `CONFIG_DDJ_FLX4_TRANSLATE_TO_P4` | `y` in `sdkconfig.defaults` | Default DDJ-FLX4 MIDI-to-P4 translator path after MVP capture validated the XML-derived map |
| `CONFIG_TINYUSB_MIDI_COUNT` | `1` | Enables MIDI driver (default is 0 → linker error) |
| `CONFIG_ESP_CONSOLE_UART_DEFAULT` | `y` | Console on UART0 (CH343 COM port) |
| `CONFIG_ESP_CONSOLE_SECONDARY_NONE` | `y` | **CRITICAL**: USB-JTAG secondary conflicts with TinyUSB |
| `CONFIG_TINYUSB_MODE_SLAVE` | `y` | DMA mode causes enumeration failure |
| `CONFIG_FREERTOS_HZ` | `1000` | 1ms tick for debounce timers |

If inherited TinyUSB MIDI compatibility stops working: first check
`CONFIG_TINYUSB_MIDI_COUNT`. If DDJ-FLX4 host capture stops working: first
confirm that `CONFIG_DDJ_FLX4_HOST_MODE=y` and that the USB-OTG port has valid
VBUS/ground wiring.

---

## Architecture

```
DDJ-FLX4 host raw logger:
  flx4_midi_host -> ESP_LOG raw packet capture

DDJ-FLX4 translator:
  flx4_midi_host -> flx4_map -> coalescing queue -> control_link UART1 -> P4

Inherited CDJ panel compatibility:
  panel_io -> panel_event_t queue -> router_task -> midi_compat + control_link
```

### Components

| Component | Description |
|-----------|------|
| `flx4_midi_host` | USB host raw logger, MIDI packet parser, FLX4 mapping helpers |
| `panel_io` | Buttons (active-low, pull-up), PCNT encoders, ADC pitch, LED output |
| `midi_compat` | Legacy TinyUSB MIDI device compatibility path |
| `control_link` | UART1 binary protocol to ESP32-P4 |
| `p4_audio_link` | S3-side I2S receiver for the P4 `hp_out` monitor PCM (BCLK7/WS8/DIN9); recovers the link sample rate and feeds `flx4_usb_audio`. `CONFIG_P4_AUDIO_LINK_ENABLED` (default-on) |
| `flx4_usb_audio` | FLX4 USB Audio Class output: drains the `p4_audio_link` monitor ring into isochronous OUT transfers to the controller headphone endpoint, tracking the active P4 link rate. `CONFIG_DDJ_FLX4_USB_AUDIO_HEADPHONES` (default-on) |
| `calibration` | Pitch fader center/deadzone/invert (GPIO1 ADC1 CH0) |
| `status_led` | XIAO onboard user LED (GPIO21 active-low): reduced link state + MIDI activity |
| `s3_debug_ap` | Runtime bench-only WPA2 SoftAP + live web log viewer (`PajoNiiiR-S3-DEBUG` / `http://192.168.4.1`); OFF at boot, toggled from P4 Settings over `CTRL_ID_S3_DEBUG_AP` (`0x85`). Default password: `PajoNiiiR`. `CONFIG_S3_DEBUG_AP_ENABLED=y` by default |
| `controller_profile` | Pure-C S3CP profile parser + table-driven MIDI-in matcher and LED-out mapper (`cp_profile_parse`, `cp_runtime_process`, `cp_profile_map_led`). No ESP deps; host-tested |
| `controller_profile_runtime` | Holds the active dynamic profile (mutex-guarded); `control_link` ACTIVATE/CLEAR installs it. `app_main` prefers it for MIDI-in map + LED-out + reconnect snapshot, falling back to `flx4_map`/`flx4_led_midi` when none is active |
| `wifi_debug_log` | Optional Wi-Fi UDP debug-log sink (`CONFIG_WIFI_DEBUG_LOG_ENABLED`, off by default); joins a configured AP and streams logs to a PC listener. Configure via `sdkconfig.wifi_debug.local` — see `docs/S3_WIFI_DEBUG_LOG.md` |

Multi-controller platform: when the P4 transfers + activates a profile, the S3
maps controller MIDI through it instead of the hard-coded FLX4 path (built-in
FLX4 map is the fallback). See `docs/CONTROLLER_PROFILE_SCHEMA.md` and the 0xA6
bulk layer in `docs/CONTROL_LINK_PROTOCOL.md`. `control_link` also carries the
`ctrl_bulk.c` frame codec + `cp_xfer.c` transfer receiver, both kept
byte-for-byte identical with the P4 copies (runner asserts).

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
| 0x01 BUTTON | S3→P4 | legacy button id or deck-aware semantic id |
| 0x02 ENCODER | S3→P4 | legacy encoder id or deck-aware jog/browse semantic id |
| 0x03 PITCH | S3→P4 | legacy pitch id or deck-aware analog semantic id |
| 0x04 HEARTBEAT | S3→P4 | id=0, val=uptime seconds |
| 0x81 LED | P4→S3 | id=led_id (0–3), val=0/1/2 (off/on/blink) |
| 0x82 STATE | both | id=CTRL_ID_S3_DEBUG_AP (0x85): P4→S3 request 0/1, S3→P4 status 0-3; also FLX4 connection state S3→P4 |
| 0xA6 BULK | both | variable-length frame `[A6][type][seq][len][payload][crc16]`: controller descriptor (S3→P4) + profile transfer (P4→S3). See `docs/CONTROL_LINK_PROTOCOL.md` |

UART1: TX=GPIO40 → P4 GPIO28, RX=GPIO41 ← P4 GPIO29, 460800 baud.

Deck-aware semantic IDs are documented in
`docs/CONTROL_LINK_PROTOCOL.md`. S3 and P4 headers are kept aligned by the
`control_link_protocol` host test.

---

## PCNT Encoders

- IDF 5.5 API: `pcnt_channel_handle_t` (not old `pcnt_unit_t`)
- X4 quadrature: two channels per unit, `accum_count=true`
- Glitch filter: 1µs
- JOG: GPIO15(A)/GPIO16(B)
- BROWSE: GPIO17(A)/GPIO18(B)

---

## Pending

- Validate DDJ-FLX4 physical USB host wiring: correct OTG port, powered hub or
  valid VBUS source, and shared ground.
- Use `docs/reference/Pioneer-DDJ-FLX4.midi.xml` and
  `docs/DDJ_FLX4_MIDI_MAP.md` as the authoritative source for remaining
  controls; physical capture is now acceptance smoke, not a coding prerequisite.
- Keep `CONFIG_DDJ_FLX4_TRANSLATE_TO_P4=y` for the normal translator build.
- Extend native FLX4 LED MIDI feedback only from P4-owned state.
- Legacy hardware acceptance: confirm LOAD/BROWSE, pitch direction, LED
  feedback, and offline heartbeat timeout on a physical S3/P4 pair if the
  inherited CDJ panel path is still used.
