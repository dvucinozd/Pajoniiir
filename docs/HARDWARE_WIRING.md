# Hardware Wiring

## Inter-Board UART

Keep the inherited CDJ100S-XXX UART wiring.

| Signal | ESP32-S3 | ESP32-P4 JC4880 JP1 | Direction |
| --- | --- | --- | --- |
| GND | GND | JP1 pin 3 or 4 | shared |
| 3.3 V | 3V3 | JP1 pin 1 or 16 | power, only if current budget is verified |
| UART TX | GPIO5 | GPIO28 / JP1 pin 19 | S3 -> P4 |
| UART RX | GPIO6 | GPIO29 / JP1 pin 12 | P4 -> S3 |

Leave the JC4880 UART0 connector free for flashing and diagnostics.

## DDJ-FLX4 To S3

The DDJ-FLX4 connects to the S3 through USB. The S3 must run USB host mode and
enumerate the FLX4 as a USB MIDI device.

For the Seeed Studio XIAO ESP32S3 / XIAO ESP32S3 Sense replacement board, use
the dedicated wiring note in `firmware/control-board-s3/PINOUT_XIAO_ESP32S3.md`.
This migration branch maps the S3-P4 UART to XIAO header pins GPIO5/GPIO6
instead of the previous DevKitC GPIO40/GPIO41 pair or the abandoned SuperMini
GPIO12/GPIO13 candidate.

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

Headphones Mix DSP is implemented in the P4 monitor path: FLX4 Headphones Mix
raw `0` is cue/PFL, raw `16383` is master, and intermediate values blend cue
with stereo master. This drives the ES8311 monitor/headphone buffer. It does
not drive the physical DDJ-FLX4 headphone jack, because the current S3 USB host
path enumerates the controller for MIDI only. Using the original FLX4 headphone
jack would require a separate USB Audio Class host/streaming phase or a hardware
analog monitor-output bridge to a headphone jack.

Important: do not short a BTL speaker amplifier output to ground. The ES8311
and amplifier path must be inspected before wiring headphones or line outputs.

## P4 Pins Mentioned In Current Plan

Inherited confirmed UART:

- P4 GPIO28: UART RX from S3 TX.
- P4 GPIO29: UART TX to S3 RX.

P4-to-S3 monitor PCM link for the FLX4 USB Audio headphones path
(DevKitC candidate hardware-validated 2026-07-02; XIAO ESP32S3/Sense wiring
validated 2026-07-06; see `docs/validation/P4_S3_AUDIO_LINK_BENCH.md` and
`docs/validation/FLX4_USB_AUDIO_E2E_SMOKE.md`):

| Signal | ESP32-P4 (JP1 pin) | ESP32-S3 side | Direction | Notes |
| --- | --- | --- | --- | --- |
| I2S BCLK | GPIO32 (JP1 pin 17) | GPIO7 | P4 -> S3 | clock for monitor PCM stream |
| I2S WS/LRCK | GPIO34 (JP1 pin 15) | GPIO8 | P4 -> S3 | stereo frame sync |
| I2S DOUT | GPIO35 (JP1 pin 13) | GPIO9 | P4 -> S3 | P4 `hp_out` monitor PCM data |
| GND | GND (JP1 pin 14) | GND | shared | required; use JP1 pin 14 next to the signal pins |
| READY/FLOW/debug | GPIO49 (JP1 pin 11) | not assigned | optional | not needed; leave disconnected |

The XIAO ESP32S3 migration set GPIO7/GPIO8/GPIO9 avoids the control UART
GPIO5/GPIO6 and UART0 GPIO43/GPIO44. These pins are legacy CDJ panel pins,
acceptable only in the `CONFIG_DDJ_FLX4_HOST_MODE` path where `panel_io` is
inactive.

**Transport details (validated):** P4 `monitor_pcm_link` is an I2S TX master
that streams `P4HP` framed blocks (stereo 16-bit monitor PCM, sequence numbers,
CRC32 over protected header plus payload). S3 `p4_audio_link` is an I2S slave
RX that deframes into a 4096-frame ring. The pipe runs at
**64 kHz 16-bit stereo slots (2.048 MHz BCLK)** -- 96 kHz
slots corrupted over the jumper harness. The TX task writes at line rate (real
blocks or explicit zero filler) so the continuously-transmitting DMA never laps
the writer mid-block. 2026-07-06 XIAO bench confirmed raw I2S reception and
deframing on GPIO7/GPIO8/GPIO9 with zero steady-state deltas for `gaps`, `crc`,
I2S `timeouts`, I2S `errors`, `underruns`, and `overruns` during a five-minute
S3-only soak. Repeat this soak after I2S, task-priority, or FLX4 USB Audio
scheduling changes.

**Product I2S unit budget (P4 rev v1.3 / eco2):** I2S unit 2 freezes on
`i2s_new_channel`, leaving units 0 and 1. Product config: **monitor link on
unit 0** (`CONFIG_MONITOR_PCM_LINK_I2S_UNIT=0`), **PCM5102A RCA MAIN on unit 1**,
and **ES8311 onboard monitor disabled** (`CONFIG_BSP_ES8311_MONITOR=n`) to free
unit 0. The FLX4 USB headphones are the CUE/MONITOR output; the local ES8311
monitor is dropped. Build both boards with the `sdkconfig.flx4_hp_e2e` profiles.

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
- Mixed sample-rate dual-deck playback is supported in the audio mixer path:
  each deck's resampler applies `source_sample_rate / output_sample_rate` on
  top of pitch, so a 48 kHz track can play correctly while the shared output is
  clocked at 44.1 kHz.
- 2026-06-27 COM15 hardware measurement with both decks playing reported
  stable decode timing, full PCM rings, and `late=0 late_max=0 us` after the
  PCM5102A sample-rate reconfiguration fix.
- The PCM5102A board's RCA and 3.5 mm connectors were hardware-smoked on
  2026-06-30 and both produced audio. Treat both as DAC board outputs for MAIN
  OUT validation; for final level/noise judgment prefer RCA or 3.5 mm into an
  active AUX/LINE IN, mixer, amplifier, or audio interface input.

PCM5102A line-out acceptance result:

1. 2026-06-30 boot probe confirmed `PCM5102A main out ready: BCLK=50 WS=52
   DOUT=51`, USB library load, and FLX4 reconnect:
   `logs/p4_pcm5102a_boot_probe_20260630_123558.log`.
2. 2026-06-30 RCA smoke confirmed playback through the PCM5102A board's RCA
   output and onboard 3.5 mm output. The capture showed
   `PCM5102A main out open @ 44100 Hz`, `late=0`, and no limiter activity for
   the Deck 1 test window:
   `logs/p4_pcm5102a_rca_smoke_20260630_123632.log`.
3. Remaining audio acceptance work is gain staging, dual-deck summed level, and
   limiter behavior; not basic PCM5102A wiring or I2S bring-up.

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
