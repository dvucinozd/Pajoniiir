# Control Board Decision: ESP32-S3 MIDI/Input Controller

Document status: historical origin of the accepted two-board split, audited
2026-07-16. The split and S3 state-ownership boundary remain valid; the original
DevKitC/CDJ-panel/USB-device implementation details below are superseded.

## Current Disposition

- The active control board is a **Seeed Studio XIAO ESP32S3**, not the original
  ESP32-S3-DevKitC-1 candidate.
- It is the USB host for the Pioneer DDJ-FLX4, maps MIDI input to semantic
  `0xA5` events, returns P4-owned LED state, streams FLX4 USB-headphone audio
  from the P4 monitor PCM link, and provides the service Debug AP/OTA path.
- P4 remains authoritative for playback, mixer, position, effect and LED state.
- R5D permanently removed the inherited GPIO panel, TinyUSB-device
  mode, `panel_io`, `midi_compat` and `calibration` product paths.

Use [`ARCHITECTURE.md`](ARCHITECTURE.md),
[`HARDWARE_WIRING.md`](HARDWARE_WIRING.md) and
[`../firmware/control-board-s3/PINOUT_XIAO_ESP32S3.md`](../firmware/control-board-s3/PINOUT_XIAO_ESP32S3.md)
for the current design and wiring. The remainder of this file is retained as
the original decision history.

## Historical Original Decision

Split the project into two cooperating boards:

1. **ESP32-P4 JC4880P443C_I_W main deck board**
   - Display and touch UI.
   - SD/USB media browsing.
   - Audio playback.
   - Deck state and visual feedback.

2. **ESP32-S3 control/MIDI board**
   - Panel buttons.
   - Jog encoder.
   - Browse encoder.
   - Pitch fader.
   - Front-panel LEDs.
   - Optional USB MIDI compatibility mode.
   - Internal event link to the ESP32-P4.

Selected candidate board: **ESP32-S3-DevKitC-1 N16R8**.

## Why ESP32-S3

ESP32-S3 is a better fit than classic ESP32-WROOM for this role because it has native USB device support. That matters if we want the control board to expose a real USB MIDI device for debugging, Mixxx compatibility, or fallback controller mode.

The ESP32-S3-DevKitC-1 is also convenient because Espressif breaks most module I/O pins out to two headers. For the N16R8-style modules, we still need to plan carefully:

- Reserve GPIO19/GPIO20 for native USB D-/D+.
- Avoid GPIO35/GPIO36/GPIO37 because they may be used internally by octal flash/PSRAM variants.
- Avoid using GPIO0 as a normal panel input because it is the boot/download strap.
- Avoid GPIO43/GPIO44 for panel wiring because they are the default UART0 TX/RX used for flashing/logging.
- Treat GPIO45/GPIO46 as strap-sensitive and avoid them unless we have a tested reason.
- Prefer ADC1 pins for the pitch fader, especially GPIO1-GPIO10 where practical.

| Candidate | Verdict | Reason |
| --- | --- | --- |
| ESP32-S3 | Recommended | Native USB device, enough GPIO, good ESP-IDF/Arduino support |
| RP2040 | Good alternative | Excellent USB MIDI and PIO, but different ecosystem |
| Teensy | Very compatible with the original controller design | Original firmware is Teensy-based, but less aligned with ESP-IDF work |
| ESP32-WROOM | Not recommended for USB MIDI | No native USB device peripheral |

## Benefits

- Avoids exhausting the limited JP1 GPIO on the ESP32-P4 board.
- Keeps high-rate controls close to a simple, dedicated controller.
- Makes wiring cleaner and easier to replace.
- Lets us test CDJ controls independently from display/audio firmware.
- Preserves a USB MIDI compatibility path close to the original Teensy-based design.
- Reduces risk on the ESP32-P4, which can focus on UI, audio, and storage.

## Trade-Offs

- Adds a second firmware project.
- Requires a small internal protocol between ESP32-S3 and ESP32-P4.
- Requires power, ground, and signal harness between boards.
- Requires versioning of the control event protocol.

The trade-off is worth it because the CDJ panel has many inputs/outputs and the ESP32-P4 board has limited accessible GPIO.

## Board Responsibilities

### ESP32-S3 Control Board

Inputs:

- 14 digital buttons.
- Jog quadrature encoder.
- Browse quadrature encoder.
- Pitch fader analog input.

Outputs:

- Cue LED.
- Play LED.
- Internal/beat LED.
- CD/end LED.

Interfaces:

- USB MIDI device for controller/debug mode.
- UART or I2C event link to ESP32-P4.
- Optional SWD/JTAG/serial debug header.

Initial pin budget for the ESP32-S3-DevKitC-1 N16R8:

| Use | Count |
| --- | ---: |
| 14 buttons | 14 |
| Jog encoder A/B | 2 |
| Browse encoder A/B | 2 |
| Pitch ADC | 1 |
| 4 LEDs | 4 |
| UART to ESP32-P4 | 2 |
| Optional reset/heartbeat/debug | 1-2 |
| Total target | 26-27 |

This is feasible, but tight. If the exact board revision exposes fewer safe pins than expected, add an I/O expander for low-rate buttons/LEDs and keep encoders plus pitch direct on the S3.

### ESP32-P4 Main Board

Responsibilities:

- Native LVGL UI on the 4.3 inch display.
- Touch input.
- Media browsing.
- Audio playback and processing.
- High-level deck state.
- Sends LED/state feedback to ESP32-S3.

## Internal Protocol Recommendation

Use **UART** first for the ESP32-S3 to ESP32-P4 link.

Why UART first:

- Simple wiring.
- Easy logging and debugging.
- No shared bus arbitration.
- No conflict with the ESP32-P4 touch/camera I2C bus.
- Good enough bandwidth for buttons, encoders, pitch, and LED feedback.

Use I2C only if wiring constraints later make it more attractive.

## Event Protocol Sketch

Keep the internal protocol separate from MIDI. MIDI remains a compatibility/debug output, while the P4 receives semantic control events.

Example event frame:

```text
0xA5 <type> <id> <value_lsb> <value_msb> <seq> <checksum>
```

Initial event types:

| Type | Meaning |
| --- | --- |
| `0x01` | Button state: id, 0/1 |
| `0x02` | Encoder delta: id, signed delta |
| `0x03` | Pitch absolute: 0-16383 |
| `0x04` | Heartbeat/status |
| `0x81` | LED command from P4 to S3 |
| `0x82` | Mode/state feedback from P4 to S3 |

This is only the starting protocol. Final implementation should include resync behavior, sequence counters, and checksum validation.

## Firmware Layout Recommendation

Future repository structure:

```text
firmware/
  main-deck-p4/
    components/
      bsp_jc4880/
      deck_core/
      ui/
      audio_engine/
      control_link/
  control-board-s3/
    components/
      panel_io/
      midi_compat/
      control_link/
      calibration/
```

## MVP Scope For ESP32-S3

First ESP32-S3 milestone:

- Read two buttons.
- Blink one LED.
- Read one encoder.
- Read pitch ADC.
- Send events over UART to a serial monitor.
- Optionally send matching USB MIDI messages to a computer.

Second milestone:

- Read all CDJ panel controls.
- Drive all LEDs.
- Send all events to ESP32-P4.
- Receive LED/state feedback from ESP32-P4.
