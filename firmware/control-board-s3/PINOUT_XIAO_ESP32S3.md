# Seeed Studio XIAO ESP32S3 Wiring For DDJ-FFL4

Documentation status: active S3 bench wiring, reviewed 2026-07-13. Revalidate
power, ground and service access in the final enclosure.

This document is the wiring reference for replacing the previous ESP32-S3
control-board candidate with the Seeed Studio XIAO ESP32S3 / XIAO ESP32S3 Sense
board shown in the 2026-07-05 bench photo.

The board is preferred over the abandoned ESP32-S3 SuperMini candidate because
it is already available on the bench and has a better mechanical USB-C
connector. It is not pin-compatible with the SuperMini: GPIO12 and GPIO13 are
not exposed, so the S3-to-P4 control link is remapped to GPIO5/GPIO6.

## Required Product Wiring

### S3 To P4 Control Link UART

Firmware file: `components/control_link/control_link_uart.c`

| Signal | XIAO ESP32S3 | ESP32-P4 JC4880P443C_I_W | Direction | Notes |
| --- | --- | --- | --- | --- |
| GND | `GND` | JP1 pin 14, 3, or 4 | shared | Required common ground |
| UART TX | `D4` / GPIO5 / SDA | GPIO28 / JP1 pin 19 | S3 -> P4 | `0xA5` control frames |
| UART RX | `D5` / GPIO6 / SCL | GPIO29 / JP1 pin 12 | P4 -> S3 | P4 LED/state frames |

Wiring:

```text
XIAO D4 / GPIO5  -> P4 GPIO28 / JP1 pin 19
XIAO D5 / GPIO6  <- P4 GPIO29 / JP1 pin 12
XIAO GND         -> P4 GND
```

Baud rate: `460800`, 8N1.

Firmware defaults on this migration branch:

```ini
CONFIG_CONTROL_LINK_UART_TX_GPIO=5
CONFIG_CONTROL_LINK_UART_RX_GPIO=6
```

### USB-UART Adapter For Runtime Monitor

Native USB-C can be used for initial flashing while the XIAO is in ROM download
mode. After the DDJ-FFL4 firmware boots in `CONFIG_DDJ_FLX4_HOST_MODE`, the
native USB peripheral is used for the FLX4 host stack, so runtime logs should
use an external USB-UART adapter.

| USB-UART adapter | XIAO ESP32S3 | ESP32-S3 signal | Notes |
| --- | --- | --- | --- |
| GND | `GND` | ground | Required |
| TX | `D7` / RX / GPIO44 | UART0 RX | Adapter TX goes to S3 RX |
| RX | `D6` / TX / GPIO43 | UART0 TX | Adapter RX goes to S3 TX |

Do not connect the adapter `5V` pin if the XIAO is already powered through
USB-C. Use only `GND`, `TX`, and `RX` for monitoring.

### DDJ-FLX4 USB Host

The DDJ-FLX4 connects to the S3 through USB. The FLX4 unit has its own power
supply, so the XIAO does not need to provide the controller's operating current.
The USB data connection still needs a valid host/OTG arrangement, shared
ground, and safe VBUS handling.

Preferred first bench wiring:

```text
XIAO USB-C -> USB-C OTG/host adapter -> FLX4 USB data port
FLX4 own power supply connected
S3/P4/FLX4 grounds common through the USB/control harness
```

Do not hard-wire two independent 5 V sources together. If VBUS must be present
for enumeration, use a protected/current-limited host VBUS path or a verified
powered hub/OTG adapter.

## Optional Product Wiring

### P4 To S3 Monitor PCM Link

This is needed only for the FLX4 USB headphones path
(`CONFIG_P4_AUDIO_LINK_ENABLED=y` on S3 and the matching P4 monitor link
configuration). It is not required for basic MIDI control.

The XIAO exposes GPIO7/GPIO8/GPIO9 on the right-side header and does not expose
GPIO10, so the optional I2S monitor link is:

| Signal | ESP32-P4 JP1 | XIAO ESP32S3 | Direction | Notes |
| --- | --- | --- | --- | --- |
| I2S BCLK | GPIO32 / JP1 pin 17 | `D8` / GPIO7 / SCK | P4 -> S3 | 2.048 MHz bench pipe |
| I2S WS/LRCK | GPIO34 / JP1 pin 15 | `D9` / GPIO8 / MISO | P4 -> S3 | Stereo frame sync |
| I2S DOUT | GPIO35 / JP1 pin 13 | `D10` / GPIO9 / MOSI | P4 -> S3 | S3 receives as DIN |
| GND | JP1 pin 14 | `GND` | shared | Required |

Firmware defaults when `CONFIG_P4_AUDIO_LINK_ENABLED=y`:

```ini
CONFIG_P4_AUDIO_LINK_BCLK_GPIO=7
CONFIG_P4_AUDIO_LINK_WS_GPIO=8
CONFIG_P4_AUDIO_LINK_DIN_GPIO=9
```

Bench status on 2026-07-06:

- S3 raw I2S RX confirmed on GPIO7/GPIO8/GPIO9.
- `P4HP` deframing confirmed.
- `P4HP` blocks now use CRC32 over the protected header plus PCM payload, so
  header corruption is rejected before sequence tracking.
- Stable S3-only 5-minute soak, with P4 left running, had zero deltas for
  sequence gaps, CRC errors, I2S read timeouts/errors, underruns, and overruns.
- Repeat the soak after I2S, task-priority, or FLX4 USB Audio scheduling
  changes.

Product e2e status on 2026-07-07:

- `build_flx4_hp_e2e_xiao` on S3 and `build_flx4_hp_e2e_tcmguard` on P4 were
  flashed and booted.
- P4 playback generated monitor PCM blocks with `MONITOR_PCM_LINK dropped=0`.
- S3 received P4 audio-link blocks on GPIO7/GPIO8/GPIO9 with `gaps=0` and
  `crc=0`.
- The FLX4 headphone jack produced audible audio after the FLX4 USB Audio
  consumer was connected.

## Pins Reserved Or Avoided

| Pin | Use / reason |
| --- | --- |
| GPIO43 / GPIO44 | UART0 console via adapter (`TX`/`RX` silkscreen pins) |
| GPIO5 / GPIO6 | S3-P4 `control_link` UART |
| GPIO7 / GPIO8 / GPIO9 | optional P4-to-S3 monitor PCM I2S link |
| GPIO19 / GPIO20 | native USB D- / D+ inside the USB-C path |
| GPIO21 | onboard XIAO user LED, active-low |
| GPIO0 / BOOT | boot strap; use only the board button, do not probe blindly |
| 5V | power only; do not tie multiple 5 V sources together without verification |
| 3V3 | 3.3 V rail; do not feed 5 V here |

## Bring-Up Checklist

1. Connect only the XIAO USB-C cable and confirm the board appears as a COM port.
2. Use ESP-IDF tools on that native port to run `chip_id` / `read_mac`.
3. Flash the S3 firmware through native USB-C before connecting the P4 harness.
4. Connect the USB-UART adapter to `D6`/`D7`/`GND` for runtime logs.
5. Boot the flashed firmware and verify application logs on the adapter COM port.
6. Wire XIAO GPIO5/GPIO6/GND to the P4 and verify heartbeat or LED frames.
7. Connect the FLX4 through a USB host/OTG path while the FLX4 uses its own
   power supply.
8. Confirm FLX4 enumeration and raw MIDI packet logs.
9. Confirm S3-to-P4 semantic control frames and P4-to-FLX4 LED feedback.
10. Only then add the optional P4-to-S3 I2S monitor PCM link and rerun the
    audio-link bench.
