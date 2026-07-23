# DDJ-FFL4 S3 Control Board Firmware - Claude Guide

Documentation status: current developer guide, audited 2026-07-21. The installed
signed release is `RC1-168-gb69f1b19` on `ota_0 / valid`, confirmed through the
P4 firmware report. It was uploaded over the S3 Debug AP (`POST /api/ota/s3`);
the S3 itself is unchanged by the P4-side recorder/service-journal work in that
release. The boards are currently **not matched** — the P4 has since moved to
`RC1-171-gacc2aa5a` for P4-only recorder instrumentation. No S3 change is
implied, but re-match both before any acceptance run. The latest full functional
hardware acceptance remains `RC1-123-g587cd7a1`; targeted Phase 20 and Beat FX
Flanger/Delay smoke is pending. Repo (2026-07-20): moved to `dvucinozd/Pajoniiir` (old
`ESP32-DDJ-FLX4` URL redirects); a single `master` branch remains after all
merged branches were pruned local + remote, with unique retired work archived
under `attic/*` tags.

## Project Overview

Seeed Studio XIAO ESP32S3 firmware for the DDJ-FFL4 control-board role. The
active target is a Pioneer DDJ-FLX4 USB MIDI host and translator feeding
deck-aware `0xA5` UART control-link frames to the ESP32-P4.

The inherited GPIO panel/TinyUSB-device path was permanently retired
in R5D. The S3 USB OTG peripheral is always the controller host.

Status:

- default build (`sdkconfig.defaults`): DDJ-FLX4 MIDI-to-P4 translator
  (`CONFIG_DDJ_FLX4_TRANSLATE_TO_P4=y`) **plus the FLX4 USB-headphone audio
  path** (`CONFIG_DDJ_FLX4_USB_AUDIO_HEADPHONES` + `CONFIG_P4_AUDIO_LINK_ENABLED`,
  folded into defaults on 2026-07-10 — a plain `idf.py build` now has sound);
- to fall back to the raw MIDI capture logger, disable
  `CONFIG_DDJ_FLX4_TRANSLATE_TO_P4` in menuconfig;
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

The Pioneer DDJ-FLX4 is the only supported operator surface. Input and LED
addresses are documented in `docs/DDJ_FLX4_MIDI_MAP.md`; no direct CDJ GPIO
button, encoder, pitch or panel LED driver remains in this target.

Onboard XIAO user status LED (`status_led` component):
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
| `CONFIG_DDJ_FLX4_TRANSLATE_TO_P4` | `y` in `sdkconfig.defaults` | Default DDJ-FLX4 MIDI-to-P4 translator path after MVP capture validated the XML-derived map |
| `CONFIG_ESP_CONSOLE_UART_DEFAULT` | `y` | Console on UART0 (CH343 COM port) |
| `CONFIG_ESP_CONSOLE_SECONDARY_NONE` | `y` | USB-JTAG secondary must not contend with USB host ownership |
| `CONFIG_FREERTOS_HZ` | `1000` | 1 ms task/timer resolution |

If DDJ-FLX4 capture stops working, confirm USB-OTG VBUS/ground wiring and inspect
the host enumeration log; there is no USB-device fallback.

---

## Architecture

```
DDJ-FLX4 host raw logger:
  flx4_midi_host -> ESP_LOG raw packet capture

DDJ-FLX4 translator:
  flx4_midi_host -> flx4_map -> coalescing queue -> control_link UART1 -> P4

```

### Components

| Component | Description |
|-----------|------|
| `flx4_midi_host` | USB host raw logger, MIDI packet parser, FLX4 mapping helpers |
| `control_link` | UART1 binary protocol to ESP32-P4 |
| `p4_audio_link` | S3-side I2S receiver for the P4 `hp_out` monitor PCM (BCLK7/WS8/DIN9); recovers the link sample rate and feeds `flx4_usb_audio`. `CONFIG_P4_AUDIO_LINK_ENABLED` (default-on) |
| `flx4_usb_audio` | FLX4 USB Audio Class output: drains the `p4_audio_link` monitor ring into isochronous OUT transfers to the controller headphone endpoint, tracking the active P4 link rate. `CONFIG_DDJ_FLX4_USB_AUDIO_HEADPHONES` (default-on) |
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

## USB Host Architecture

The ESP32-S3 USB OTG peripheral on GPIO19/20 is unconditionally the FLX4 host.
R5D removed the alternative TinyUSB MIDI-device product mode and its direct
dependency. Use the external CH343 UART bridge for flashing/logging while the
XIAO USB-C/OTG connection enumerates the externally powered FLX4. Preserve a
valid host-VBUS arrangement and shared ground as documented in
`docs/HARDWARE_WIRING.md`.

---

## UART Control Link Protocol

```
[0xA5][type][id][val_lo][val_hi][seq][checksum]
checksum = type ^ id ^ val_lo ^ val_hi ^ seq
```

| Type | Direction | Content |
|------|-------|---------|
| 0x01 BUTTON | S3→P4 | deck-aware semantic button id |
| 0x02 ENCODER | S3→P4 | deck-aware relative jog/browse semantic id |
| 0x03 PITCH | S3→P4 | deck-aware absolute-control semantic id |
| 0x04 HEARTBEAT | S3→P4 | id=0, val=uptime seconds |
| 0x81 LED | P4→S3 | P4-owned shared LED id and value |
| 0x82 STATE | both | id=CTRL_ID_S3_DEBUG_AP (0x85): P4→S3 request 0/1, S3→P4 status 0-3; also FLX4 connection state S3→P4 |
| 0xA6 BULK | both | variable-length frame `[A6][type][seq][len][payload][crc16]`: controller descriptor (S3→P4) + profile transfer (P4→S3). See `docs/CONTROL_LINK_PROTOCOL.md` |

UART1: TX=GPIO5 → P4 GPIO28, RX=GPIO6 ← P4 GPIO29, 460800 baud.

Deck-aware semantic IDs are documented in
`docs/CONTROL_LINK_PROTOCOL.md`. S3 and P4 headers are kept aligned by the
`control_link_protocol` host test.

---

## Retired Direct-Panel Notes

The former PCNT jog/browse encoders, direct pitch ADC, CDJ buttons and panel LED
GPIO assignments were removed with the legacy product mode in R5D. Current jog,
browse, pitch, buttons and LEDs travel through the FLX4 USB MIDI path; do not use
the old direct-panel GPIO assignments as wiring instructions.

---

## Pending

- Revalidate DDJ-FLX4 OTG/VBUS/shared-ground wiring after final enclosure or
  power-topology changes; the current bench enumeration path is accepted.
- Use `docs/reference/Pioneer-DDJ-FLX4.midi.xml` and
  `docs/DDJ_FLX4_MIDI_MAP.md` as the authoritative source for remaining
  controls; physical capture is now acceptance smoke, not a coding prerequisite.
- Keep `CONFIG_DDJ_FLX4_TRANSLATE_TO_P4=y` for the normal translator build.
- Extend native FLX4 LED MIDI feedback only from P4-owned state.
- Watch for sample-rate mismatch between the P4 monitor PCM producer and the
  FLX4 USB Audio consumer during hardware soak.
