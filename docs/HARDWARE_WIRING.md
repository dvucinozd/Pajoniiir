# Hardware Wiring

## Inter-Board UART

Keep the inherited CDJ100S-XXX UART wiring.

| Signal | ESP32-S3 | ESP32-P4 JC4880 JP1 | Direction |
| --- | --- | --- | --- |
| GND | GND | JP1 pin 3 or 4 | shared |
| 3.3 V | 3V3 | JP1 pin 1 or 16 | power, only if current budget is verified |
| UART TX | GPIO40 | GPIO28 / JP1 pin 19 | S3 -> P4 |
| UART RX | GPIO41 | GPIO29 / JP1 pin 12 | P4 -> S3 |

Leave the JC4880 UART0 connector free for flashing and diagnostics.

## DDJ-FLX4 To S3

The DDJ-FLX4 connects to the S3 through USB. The S3 must run USB host mode and
enumerate the FLX4 as a USB MIDI device.

Open wiring questions:

- confirm whether the selected S3 dev board can power the FLX4 through USB;
- if not, use a powered USB hub or externally powered USB host wiring;
- ensure common ground between S3, P4, audio boards, and any external supply;
- document actual VBUS/current behavior during bench testing.

## P4 Audio Outputs

The inherited P4 firmware currently uses ES8311/I2S for audio output.

DDJ-FFL4 needs two output paths:

- master output for speakers/PA/recording;
- cue/PFL output for headphones.

Recommended MVP hardware plan:

| Output | Hardware | Notes |
| --- | --- | --- |
| Master | external PCM5102A or similar I2S DAC | use spare verified P4 GPIOs for BCLK/LRCK/DOUT or a second I2S path |
| Cue/PFL | onboard ES8311 path | verify electrical safety before using board speaker output wiring |

Important: do not short a BTL speaker amplifier output to ground. The ES8311
and amplifier path must be inspected before wiring headphones or line outputs.

## P4 Pins Mentioned In Current Plan

Inherited confirmed UART:

- P4 GPIO28: UART RX from S3 TX.
- P4 GPIO29: UART TX to S3 RX.

Candidate external DAC pins from the project note:

- GPIO30
- GPIO31
- GPIO32

These must be verified against the JC4880 schematic, board examples, and actual
ESP-IDF I2S peripheral routing before committing PCB or harness work.

## Bench Bring-Up Order

1. Power S3 and P4 independently and confirm shared ground.
2. Verify S3/P4 UART heartbeat with no FLX4 connected.
3. Connect FLX4 to S3 and capture raw USB MIDI input.
4. Forward only Play/Cue/Load events to P4.
5. Add tempo/fader/crossfader events after raw ranges are confirmed.
6. Add LED feedback after P4 state transitions are stable.
7. Add external DAC wiring only after dual-deck software mixer can produce test
   buffers on a single known-good output.
