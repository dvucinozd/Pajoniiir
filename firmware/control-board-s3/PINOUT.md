# ESP32-S3-DevKitC-1 N16R8 — Pinout Scheme

Documentation status: retired hardware reference, reviewed 2026-07-13. R5D
removed the corresponding `panel_io`, `calibration` and TinyUSB-device firmware;
none of the GPIO panel wiring below is supported by the product build. The
active compact build uses `PINOUT_XIAO_ESP32S3.md`.

Complete wiring scheme for the imported historical control board firmware.  
Values below document the imported historical design and are no longer mapped
by S3 source code.

---

## Buttons — 14 Buttons, active-low

Each button connects between the GPIO pin and GND.  
**External pull-up resistors are not required** — internal pull-ups are enabled in firmware.

| Signal | GPIO | CDJ Function | MIDI Note (Ch1) |
|--------|------|--------------|-----------------|
| BTN_EJECT | **GPIO2** | Eject | 63 (0x3F) |
| BTN_TRACK_PREV | **GPIO3** | Track ← | 64 (0x40) |
| BTN_TRACK_NEXT | **GPIO4** | Track → | 65 (0x41) |
| BTN_SEARCH_BACK | **GPIO5** | Search ◀◀ | 66 (0x42) |
| BTN_SEARCH_FWD | **GPIO6** | Search ▶▶ | 67 (0x43) |
| BTN_CUE | **GPIO7** | Cue | 61 (0x3D) |
| BTN_PLAY | **GPIO8** | Play/Pause | 60 (0x3C) |
| BTN_PERF1 | **GPIO9** | Jet | 68 (0x44) |
| BTN_PERF2 | **GPIO10** | Zip | 69 (0x45) |
| BTN_PERF3 | **GPIO11** | Wah | 70 (0x46) |
| BTN_HOLD | **GPIO12** | Hold | 71 (0x47) |
| BTN_MODE | **GPIO13** | Time/Auto Cue | 72 (0x48) |
| BTN_MASTER_TEMPO | **GPIO14** | Master Tempo | 62 (0x3E) |
| BTN_LOAD | **GPIO21** | Load selected track | 73 (0x49) |

```
GPIO_x ──── [button] ──── GND
```

> Button press = GPIO LOW → Note On (velocity 127)  
> Button release = GPIO HIGH → Note Off (velocity 0)

---

## Encoders — Jog + Browse (X4 quadrature)

PCNT hardware driver, 1 µs glitch filter. Connect A/B phases directly without pull-up resistors.

| Signal | GPIO | Description |
|--------|------|-------------|
| JOG_A | **GPIO15** | Jog wheel — phase A |
| JOG_B | **GPIO16** | Jog wheel — phase B |
| BROWSE_A | **GPIO17** | Browse encoder — phase A |
| BROWSE_B | **GPIO18** | Browse encoder — phase B |

```
Encoder (3-pin):
  GND    ──── Encoder GND pin
  GPIO15 ──── Phase A
  GPIO16 ──── Phase B

Encoder (4-pin with VCC):
  3.3V   ──── Encoder VCC pin
  GND    ──── Encoder GND pin
  GPIO15 ──── Phase A
  GPIO16 ──── Phase B
```

**MIDI Mapping:**
- Jog CW: Ch2 CC20 = 65 | Jog CCW: Ch2 CC20 = 63
- Browse CW/CCW: Ch3 Note 70/71 impulse

---

## Pitch Fader — ADC Potentiometer (14-bit)

| Signal | GPIO | ADC Channel |
|--------|------|-----------|
| PITCH | **GPIO1** | ADC1 CH0 |

```
3.3V ──── [top terminal of pot] ──┐
                                  ├── GPIO1 (middle/wiper pin)
GND  ──── [bottom terminal of pot] ──┘
```

- Voltage: **3.3V** (do NOT use 5V — ADC maximum is 3.3V)
- Attenuation: 12 dB (full scale range 0–3.3V)
- Resolution: 12-bit raw (0–4095) → scaled to 14-bit (0–16383)
- MIDI: Ch1 CC0 (MSB) + Ch1 CC32 (LSB) — 14-bit pitch

**Calibration** (in `components/calibration/calibration.c`):

| Parameter | Value |
|-----------|-----------|
| Center | 8192 (fader center) |
| Deadzone | ±200 (center deadzone) |
| Invert | true (invert direction if needed) |

---

## LEDs — 4 LED Indicators, active-high

| Signal | GPIO | CDJ Function | MIDI Trigger |
|--------|------|--------------|--------------|
| LED_CUE | **GPIO33** | Cue Point | Ch1 Note 62 On/Off |
| LED_PLAY | **GPIO34** | Play Indicator | Ch1 Note 61 On/Off |
| LED_BEAT | **GPIO38** | Beat / sync | Ch1 Note 63 On/Off |
| LED_END | **GPIO39** | End of track | Ch1 Note 64 On/Off |

```
GPIO_x ──── [220Ω] ──── [LED+] ──── [LED-] ──── GND
```

| Resistor | Current | Note |
|----------|---------|------|
| 220 Ω | ~10 mA | Full brightness |
| 470 Ω | ~5 mA | Moderate brightness |
| 1 kΩ | ~2.5 mA | Low brightness |

> Blink: the firmware supports blink mode with a period (ms) — controlled by the host via Note 2 (value=2)

---

## UART Control Link — ESP32-S3 ↔ ESP32-P4

For connecting to the main board (ESP32-P4). Keep disconnected until the P4 board is mounted.

| Signal | GPIO | Direction | Baud |
|--------|------|-------|------|
| UART1_TX | **GPIO40** | S3 → P4 | 460800 |
| UART1_RX | **GPIO41** | P4 → S3 | 460800 |

```
ESP32-S3              ESP32-P4
  GPIO40 (TX) ──────── RX
  GPIO41 (RX) ──────── TX
  GND         ──────── GND   ← required!
```

**Frame Format** (7 bytes):
```
[0xA5] [type] [id] [val_lo] [val_hi] [seq] [checksum]
checksum = type ^ id ^ val_lo ^ val_hi ^ seq
```

---

## Candidate P4 Monitor PCM Link — DDJ-FLX4 USB Audio Headphones

This candidate mapping is historical. The accepted XIAO wiring is documented
in `PINOUT_XIAO_ESP32S3.md`; the retired CDJ panel path is no longer selectable.

| Signal | ESP32-S3 candidate | ESP32-P4 candidate | Direction |
|--------|--------------------|--------------------|-----------|
| I2S BCLK | GPIO15 | GPIO32 | P4 -> S3 |
| I2S WS/LRCK | GPIO16 | GPIO34 | P4 -> S3 |
| I2S DIN | GPIO17 | GPIO35 | P4 -> S3 |
| READY/FLOW/debug | GPIO18 | GPIO49 | optional, direction selected by role |
| GND | GND | GND | shared |

Notes:

- GPIO15 and GPIO16 are legacy `JOG_A` / `JOG_B` pins in panel mode.
- GPIO17 and GPIO18 are legacy `BROWSE_A` / `BROWSE_B` pins in panel mode.
- This mapping intentionally avoids GPIO36/GPIO37 because repo source comments
  still flag GPIO35-GPIO37 as octal-PSRAM-sensitive on N16R8 variants.
- GPIO48 is intentionally left unused for this PCM link because it is reserved
  for future LED work.
- Do not use this mapping for the inherited CDJ panel compatibility mode.

---

## USB — MIDI Device (TinyUSB)

| Parameter | Value |
|-----------|-----------|
| GPIO D− | **GPIO19** |
| GPIO D+ | **GPIO20** |
| VID | 0x303A (Espressif) |
| PID | 0x4008 |
| Manufacturer | DIY CDJ |
| Product | (historical device) |
| MIDI channel buttons | Ch1 (0x90/0x80) |
| MIDI channel jog | Ch2 (0xB1 CC20) |
| MIDI channel browse | Ch3 (0x92/0x82) |
| MIDI pitch | Ch1 CC0+CC32 (14-bit) |

---

## Console / Flashing

| Connector | COM Port | Chip | Purpose |
|----------|----------|------|---------|
| UART0 (GPIO43/44) | **COM4** | CH343 | Logs + flashing |
| USB-OTG (GPIO19/20) | — | TinyUSB | MIDI device to host |

```bash
# Flash
idf.py -p COM4 flash

# Flash + logs
idf.py -p COM4 flash monitor
```

---

## Reserved Pins — DO NOT USE

| GPIO | Reason for Reservation |
|------|--------------------|
| GPIO0 | Boot strapping pin |
| GPIO19, 20 | USB D−/D+ (TinyUSB MIDI) |
| GPIO26–32 | SPI Flash interface |
| GPIO43 | UART0 TX (console) |
| GPIO44 | UART0 RX (console) |
| GPIO45, 46 | Strapping pins (sensitive) |

---

## Free GPIOs on DevKitC-1 N16R8

```
Occupied: 1–18, 21, 33, 34, 38–41
Free:     22–25, 35–37, 42, 47, 48
          (GPIO48 = RGB LED on the DevKitC-1 board)
```

---

## Quick Reference Diagram

```
                    ESP32-S3-DevKitC-1 N16R8
                   ┌─────────────────────────┐
             3.3V ─┤ 3V3               GND   ├─ GND
              GND ─┤ GND               IO43  ├─ UART0 TX (console)
         PITCH ───┤ IO1               IO44  ├─ UART0 RX (console)
      BTN_EJECT ──┤ IO2               IO42  ├─ (free)
    BTN_TRACK_PV ──┤ IO3               IO41  ├─ UART1 RX (P4)
    BTN_TRACK_NX ──┤ IO4               IO40  ├─ UART1 TX (P4)
   BTN_SRCH_BACK ──┤ IO5               IO39  ├─ LED_END
    BTN_SRCH_FWD ──┤ IO6               IO38  ├─ LED_BEAT
         BTN_CUE ──┤ IO7               IO37  ├─ (free)
        BTN_PLAY ──┤ IO8               IO36  ├─ (free)
       BTN_PERF1 ──┤ IO9               IO35  ├─ (free)
       BTN_PERF2 ──┤ IO10              IO34  ├─ LED_PLAY
       BTN_PERF3 ──┤ IO11              IO33  ├─ LED_CUE
        BTN_HOLD ──┤ IO12              IO26  ├─ (SPI Flash)
        BTN_MODE ──┤ IO13              IO21  ├─ BTN_LOAD
   BTN_MST_TEMPO ──┤ IO14              IO20  ├─ USB D+ (TinyUSB)
         JOG_A ────┤ IO15              IO19  ├─ USB D− (TinyUSB)
         JOG_B ────┤ IO16              IO18  ├─ BROWSE_B
      BROWSE_A ────┤ IO17              IO48  ├─ RGB LED (board)
                   └─────────────────────────┘
```

---

*Generated: 2026-05-20 | Firmware: ESP-IDF v5.5 | Board: ESP32-S3-DevKitC-1 N16R8*
