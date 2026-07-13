# XDJ100SX Source Project Analysis

Document status: historical upstream analysis, reviewed 2026-07-13. It
describes a source project, not current DDJ-FFL4 architecture.

Source reviewed: `../upstream/XDJ100SX`, cloned from `https://github.com/marcmonka/XDJ100SX`.

## Project Intent

XDJ100SX converts an old Pioneer CDJ-100S into a standalone single-deck player. The original design is a small Linux computer running Mixxx, with a Teensy LC used as a USB MIDI interface for the CDJ buttons, LEDs, jog encoder, browse encoder, and pitch slider.

The important point for our port: the repo is not a single firmware application. It is a system made of four cooperating layers:

- CDJ front panel hardware wiring.
- Teensy LC Arduino firmware that converts hardware events to USB MIDI.
- Mixxx MIDI mapping and JavaScript behavior.
- Mixxx skin plus Raspberry Pi OS boot setup.

## Repository Inventory

- `arduino/XDJ100SX.ino`
  - Teensy LC firmware.
  - Reads 13 original buttons plus one added load button.
  - Reads two quadrature encoders: jog and browse.
  - Reads pitch fader as 14-bit MIDI through MSB/LSB CC messages.
  - Receives MIDI notes back from Mixxx and drives four LEDs.

- `arduino/name.c`
  - Changes the USB MIDI product name to `XDJ100SX`.
  - Teensy-specific, not reusable directly on ESP32-P4.

- `mixxx/MIDI/XDJ100SX.midi.xml`
  - Mixxx controller preset.
  - Maps MIDI notes/CCs to Mixxx controls and JavaScript callbacks.

- `mixxx/MIDI/XDJ100SX.js`
  - Higher-level behavior:
    - jog nudge/scratch routing,
    - search button behavior changes when jog was recently active,
    - master tempo and tempo range with shift,
    - six performance modes,
    - hot cues, loop roll, beat jump, key shift,
    - browse encoder and load/back behavior,
    - 14-bit pitch fader normalization.

- `mixxx/SKIN/XDJ100SX/`
  - Custom one-deck Mixxx skin.
  - Sets `[Master],num_decks` to `1`.
  - Uses tabs: overview, library, hotcues, beatloop, keyshift, beatjump, stems.
  - Minimum size is `480,420`, while the new board is portrait `480x800` and likely needs landscape rotation for a CDJ-like screen.

- `docs/XDJ100SX.pdf`
  - Build documentation.
  - Identifies the original hardware I/O count and the mechanical/electrical modification flow.

- `datasheets/`
  - CDJ-100S service manual, Teensy LC card, Raspberry Pi schematic.

## Original Hardware I/O Contract

The XDJ100SX documentation lists these control signals:

| Function | Direction | Signal type | Count |
| --- | --- | --- | --- |
| Buttons: eject, track prev/next, search back/forward, cue, play, jet, zip, wah, hold, time, master tempo, load | Input | Digital, pull-up button to GND | 14 |
| Pitch slider | Input | Analog | 1 |
| Jog wheel | Input | Quadrature digital | 2 |
| Browse encoder | Input | Quadrature digital | 2 |
| LEDs: cue, play, internal beat, CD/end | Output | Digital | 4 |
| Total | Mixed | Digital + analog | 23 |

Original Teensy pin assignment from firmware:

| Function | Teensy pin |
| --- | --- |
| Eject | 0 |
| Track previous | 1 |
| Track next | 2 |
| Search back | 3 |
| Search forward | 4 |
| Cue | 5 |
| Play | 6 |
| Jet | 7 |
| Zip | 8 |
| Wah | 9 |
| Hold | 10 |
| Time | 11 |
| Master tempo | 12 |
| Pitch | A0 |
| Jog A/B | 20 / 15 |
| Cue LED | 16 |
| Play LED | 17 |
| Internal LED | 18 |
| CD LED | 19 |
| Browse A/B | 22 / 21 |
| Load | 23 |

## MIDI Contract

The MIDI map is useful as a behavioral spec even if we do not keep Mixxx.

Inputs sent by firmware:

| Control | MIDI |
| --- | --- |
| Play | Ch 1 Note 60 / `0x3C` |
| Cue | Ch 1 Note 61 / `0x3D` |
| Master tempo | Ch 1 Note 62 / `0x3E` |
| Eject / back | Ch 1 Note 63 / `0x3F` |
| Track previous | Ch 1 Note 64 / `0x40` |
| Track next | Ch 1 Note 65 / `0x41` |
| Search back | Ch 1 Note 66 / `0x42` |
| Search forward | Ch 1 Note 67 / `0x43` |
| Jet | Ch 1 Note 68 / `0x44` |
| Zip | Ch 1 Note 69 / `0x45` |
| Wah | Ch 1 Note 70 / `0x46` |
| Hold | Ch 1 Note 71 / `0x47` |
| Time / mode | Ch 1 Note 72 / `0x48` |
| Load | Ch 1 Note 73 / `0x49` |
| Jog | Ch 2 CC 20 / `0x14`, values 63/65 |
| Browse down/up | Ch 3 Notes 70/71 |
| Pitch | Ch 1 CC 0 + CC 32 as 14-bit value |

LEDs expected by firmware:

| LED behavior | MIDI note |
| --- | --- |
| Play LED | Note 61 |
| Cue LED | Note 62 |
| Internal beat LED | Note 63, gated by play state |
| CD/end LED | Note 64, blink in firmware |
| Play state helper | Note 65 |

## Reusable Parts

Directly reusable:

- Functional mapping from physical controls to deck actions.
- Button mode state machine from `XDJ100SX.js`.
- Pitch normalization formula.
- Jog direction semantics.
- LED behavior model.
- Visual structure of the one-deck skin.

Reusable only as reference:

- Mixxx skin XML/QSS. LVGL will need a native UI, not Mixxx skin files.
- Mixxx MIDI XML. Native firmware can use it as a control matrix, but not execute it.
- Arduino Teensy libraries: `Bounce`, `Encoder`, `ResponsiveAnalogRead`, `usbMIDI`.

Not reusable on the target board:

- Raspberry Pi OS setup and Openbox/Mixxx boot flow.
- Teensy USB naming code.
- Direct Teensy pin assignment.

## Porting Consequence

There are two possible meanings of "port":

1. Controller-display companion:
   - Keep Mixxx on Raspberry Pi or another Linux computer.
   - Use the ESP32-P4 board as touchscreen/control surface and maybe audio/display helper.
   - Lowest risk, closest to upstream behavior.

2. Native standalone deck on ESP32-P4:
   - Replace Mixxx with ESP32 firmware.
   - Implement file browsing, audio playback, waveform/overview, cue/loop/tempo behavior, UI, storage, and control handling natively.
   - This is the real port to the JC4880P443C_I_W board, but it is a rewrite using the upstream project as product spec.

Recommended path: build the native standalone deck in stages, while keeping a USB-MIDI compatibility mode as a debug and fallback path.

