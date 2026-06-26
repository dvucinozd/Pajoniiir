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
| Master | external PCM5102A or similar I2S DAC | use the JP1 candidate set GPIO50/GPIO52/GPIO51 only after bench verification |
| Cue/PFL | onboard ES8311 path | ES8311 remains the monitor/headphones/onboard-speaker path |

Important: do not short a BTL speaker amplifier output to ground. The ES8311
and amplifier path must be inspected before wiring headphones or line outputs.

## P4 Pins Mentioned In Current Plan

Inherited confirmed UART:

- P4 GPIO28: UART RX from S3 TX.
- P4 GPIO29: UART TX to S3 RX.

PCM5102A MAIN OUT candidate pins for the photographed PCM5102MK/PCM5102A
breakout board. The board header silkscreen is:

```text
VCC
GND
GND
LRCK
DATA
BCK
```

`DATA` on this module is the DAC serial data input and must be driven by the
P4 I2S DOUT signal. `LRCK` is the same signal as I2S `WS`.

| PCM board header | Signal meaning | ESP32-P4 JC4880 JP1 candidate |
| --- | --- | --- |
| VCC | DAC board power | 3.3 V first; use 5 V only if this exact module requires it |
| GND | ground | GND |
| GND | ground | GND, optional second return |
| LRCK | I2S word select / left-right clock | GPIO52 / JP1 pin 5 |
| DATA | I2S serial data into DAC | GPIO51 / JP1 pin 7 |
| BCK | I2S bit clock | GPIO50 / JP1 pin 9 |

No MCLK/SCK pin is exposed on this module, so firmware keeps PCM5102A MCLK as
`I2S_GPIO_UNUSED` for first bring-up.

Runtime notes after hardware bring-up:

- PCM5102A uses the P4 I2S1 channel as MAIN OUT when
  `CONFIG_BSP_PCM5102A_MAIN_OUT=y` is enabled in the local P4 build config.
- ES8311 remains the monitor/onboard-speaker path and continues to receive the
  monitor/headphone buffer.
- The audio engine must reconfigure the PCM5102A I2S clock to the current track
  sample rate when opening the shared output service. Leaving PCM5102A at its
  44.1 kHz BSP default while playing a 48 kHz track causes slow/popping audio
  and output-late diagnostics.
- 2026-06-27 COM15 hardware measurement with both decks playing reported
  stable decode timing, full PCM rings, and `late=0 late_max=0 us` after the
  PCM5102A sample-rate reconfiguration fix.

Rejected DAC pin proposal:

- GPIO22/GPIO23/GPIO24/GPIO25 must not be used for this DAC plan.
- GPIO23 is already LCD backlight PWM.

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
