# CDJ Front Panel Wiring Map

Maps CDJ-100S front panel signals to ESP32-S3 GPIO assignments.  
V1 firmware is live — use this as the physical wiring reference.  
Full pinout with connection diagrams: `firmware/control-board-s3/PINOUT.md`

## Wiring Rules

- All CDJ front-panel grounds tied to ESP32-S3 GND.
- Buttons: active-low, internal pull-up enabled in firmware — no external resistor needed.
- LEDs: active-high, **220Ω series resistor** required (GPIO → 220Ω → LED+ → LED− → GND).
- Pitch fader: 3.3V supply only — do NOT connect to 5V.
- Keep harness removable during development.
- LOAD and BROWSE are implemented in firmware; wire GPIO21 and GPIO17/18 when the panel has those controls available.

---

## Inter-Board UART Link (S3 ↔ P4)

| Signal | ESP32-S3 GPIO | ESP32-P4 JP1 pin | Direction | Confirmed |
| --- | --- | --- | --- | --- |
| GND | GND | JP1 pin 3 or 4 | Shared | Yes |
| 3.3V | 3V3 | JP1 pin 1 or 16 | Power | Yes |
| UART TX (events) | GPIO40 | GPIO28 (JP1 pin 19) | S3→P4 | Yes ✅ |
| UART RX (LED cmds) | GPIO41 | GPIO29 (JP1 pin 12) | P4→S3 | Yes ✅ |

Protocol: 115200 baud, 7-byte frames `[0xA5][type][id][val_lo][val_hi][seq][checksum]`

> [!WARNING]
> **JC4880P443C MX 1.25 4-pin UART Connector**:
> There is an `MX 1.25 4P` connector on the P4 board labeled **CN2 (INPUT)**. This connector is connected to **UART0** (TX=GPIO36, RX=GPIO37) and is wired in parallel with the onboard USB-to-UART chip used for programming and system debug logs (serial monitor).
> 
> **Can I use this connector to connect the S3?**
> Theoretically yes, but **it is not recommended**. If you connect the S3 to this port, signal conflicts with the onboard USB-to-UART chip will occur, causing:
> 1. Inability to use the serial monitor for debugging at the same time.
> 2. Problems during every attempt to flash new firmware (you will have to physically disconnect the S3).
> 
> **Recommendation**: Connect the S3 to the **JP1 (Expand IO)** connector using **GPIO28 (RX)** and **GPIO29 (TX)** as specified in the table above, leaving the UART0 port (MX 1.25) free exclusively for flashing and diagnostics.


---

## Buttons — 14 Buttons

Active-low. Connect between GPIO and GND. No external pull-up needed.

| Function | ESP32-S3 GPIO | CDJ-100S label | MIDI Note Ch1 | Wired? |
| --- | --- | --- | --- | --- |
| Eject | GPIO2 | EJECT | 63 (0x3F) | No |
| Track ← | GPIO3 | TRACK SEARCH ◀ | 64 (0x40) | No |
| Track → | GPIO4 | TRACK SEARCH ▶ | 65 (0x41) | No |
| Search ◀◀ | GPIO5 | SEARCH ◀◀ | 66 (0x42) | No |
| Search ▶▶ | GPIO6 | SEARCH ▶▶ | 67 (0x43) | No |
| Cue | GPIO7 | CUE | 61 (0x3D) | No |
| Play/Pause | GPIO8 | PLAY/PAUSE | 60 (0x3C) | No |
| Jet | GPIO9 | JET | 68 (0x44) | No |
| Zip | GPIO10 | ZIP | 69 (0x45) | No |
| Wah | GPIO11 | WAH | 70 (0x46) | No |
| Hold | GPIO12 | HOLD | 71 (0x47) | No |
| Time/Auto Cue | GPIO13 | TIME MODE/AUTO CUE | 72 (0x48) | No |
| Master Tempo | GPIO14 | MASTER TEMPO | 62 (0x3E) | No |
| Load | GPIO21 | LOAD | 73 (0x49) | No |

---

## Jog + Browse Encoders

X4 quadrature, PCNT hardware driver, 1µs glitch filter. No pull-up needed.

| Signal | ESP32-S3 GPIO | MIDI | Wired? |
| --- | --- | --- | --- |
| JOG_A | GPIO15 | Ch2 CC20: CW=65, CCW=63 | No |
| JOG_B | GPIO16 | | No |
| BROWSE_A | GPIO17 | Ch3 Notes: CW=70, CCW=71 | No |
| BROWSE_B | GPIO18 | | No |

```
Jog encoder GND → ESP32-S3 GND
Jog encoder A   → GPIO15
Jog encoder B   → GPIO16
(4-pin: VCC     → 3.3V)

Browse encoder GND → ESP32-S3 GND
Browse encoder A   → GPIO17
Browse encoder B   → GPIO18
(4-pin: VCC        → 3.3V)
```

---

## Pitch Slider

| Signal | ESP32-S3 GPIO | Notes |
| --- | --- | --- |
| Wiper (center) | GPIO1 (ADC1 CH0) | 3.3V max — do NOT use 5V |
| Top | 3.3V | |
| Bottom | GND | |

MIDI: Ch1 CC0 (MSB) + Ch1 CC32 (LSB) — 14-bit.  
Calibration: center=8192, deadzone=±200, invert=true (in `components/calibration/`).

---

## LEDs — 4 LEDs

Active-high. 220Ω series resistor required.

| Function | ESP32-S3 GPIO | CDJ-100S label | MIDI trigger | Wired? |
| --- | --- | --- | --- | --- |
| Cue point | GPIO33 | CUE LED | Ch1 Note 62 | No |
| Play indicator | GPIO34 | PLAY LED | Ch1 Note 61 | No |
| Beat / sync | GPIO38 | BEAT LED | Ch1 Note 63 | No |
| End of track | GPIO39 | CD/END LED | Ch1 Note 64 | No |

```
GPIO_x → [220Ω] → [LED+] → [LED−] → GND
```

---

## S3/P4 Compatibility Checklist

| Check | Expected |
| --- | --- |
| UART wiring | S3 GPIO40 TX to P4 GPIO28 RX, S3 GPIO41 RX to P4 GPIO29 TX, shared GND |
| Heartbeat | P4 Settings shows S3 Connected within 5 s, Offline after roughly 10 s without heartbeat |
| LED feedback | P4 drives S3 PLAY/CUE/BEAT/END LED commands over UART |
| Play/Cue | S3 PLAY toggles audio, CUE returns to cue and pauses |
| Browse/Load | Browse encoder moves library selection; LOAD loads selected track |
| Pitch | Fader changes pitch label/audio in the expected CDJ direction |
| Jog | Jog id=0 still scratches/nudges; browse id=1 does not affect jog |
