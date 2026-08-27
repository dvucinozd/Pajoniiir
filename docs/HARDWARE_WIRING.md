# Hardware Wiring

> **Active `feat/p4-dual-usb-host` wiring:** the S3 is removed. Connect the
> Rekordbox medium to P4 USB0 and the DDJ-FLX4 directly to P4 USB1. FLX4 MIDI IN,
> MIDI OUT and four-channel USB Audio all terminate on P4; cue is sent on UAC
> channels 3/4 while PCM5102A remains MAIN. Do not wire the former S3/P4 UART or
> monitor-I2S link. Their tables below are historical evidence only and do not
> describe installable product wiring. The protected downstream VBUS design in
> the next section is still a
> mandatory unresolved gate.

Status: current bench wiring, updated 2026-08-12. Revalidate cable routing,
power budget, cooling and RF behavior after final enclosure installation.

## P4 Dual-USB VBUS Blocker

The experimental `feat/p4-dual-usb-host` topology uses USB0 for the Rekordbox
drive and USB1 (the former COM15 connector) for the DDJ-FLX4. A 2026-08-10
bench attempt established that feeding 5 V to `VCC5V` on JP1 pin 2 powers the
JC4880P443C_I_W itself but does **not** provide usable downstream VBUS to either
USB-C host port in that arrangement. The drive LED stayed off and neither
device enumerated, while the P4 remained healthy and reachable over Wi-Fi.

On 2026-08-12 a later, still electrically unqualified bench arrangement did
power both devices: USB0 mounted the 191-track drive and USB1 enumerated the
FLX4 directly. This proves the firmware topology, not the safety or capacity of
the power path. Dual-deck playback then produced a hardware-reported brownout
after about 6.5 seconds even with the FLX4 disconnected, while audio deadline
and underrun counters remained clean. The supply path is therefore still a
merge blocker. Do not inject raw 5 V into USB-C pins, join separate supplies
with a passive Y-cable or assume that JP1 power is forwarded to USB VBUS.

The required electrical topology for each root port is:

```text
P4 root D+  ------------------------------  device D+
P4 root D-  ------------------------------  device D-
P4 root GND ------------------------------  device GND
P4 root VBUS ---- isolated, no connection --X
protected 5 V output ---------------------  device VBUS
```

This topology may be implemented with one data interposer per port, but the
preferred permanent implementation is a small internal power-distribution
daughterboard. Feed the P4 and both protected USB outputs from one common
regulated supply:

```text
regulated 5 V supply
        |
        +-- fuse/eFuse -------------------- JP1 pin 2 (P4 VCC5V)
        |
        +-- dual current-limited USB switch
                +-- OUT1 ------------------ USB0 device-side VBUS (drive)
                +-- OUT2 ------------------ USB1 device-side VBUS (FLX4)

common GND -------------------------------- P4 + both USB devices
```

The native P4-side VBUS conductor must be physically isolated from each
device-side VBUS before either protected output is connected. D+/D-, ground
and shield remain continuous to the intended P4 root port. The same result may
instead be obtained by cutting the two native VBUS traces and injecting the
protected outputs at the connector pads, but that is an invasive board
modification and must be continuity-checked before power-up.

Use one independently protected high-side output per port. A TPS2561-class
dual USB power switch is a candidate; configure it disabled by default, add
local decoupling and verify the current-limit resistor against the exact part
datasheet. The provisional target is approximately 0.8--1.0 A per port and a
5 V / 3 A common supply; a quality 4 A source provides useful wiring and
startup margin, but neither rating replaces real current and voltage-drop
measurements. Keep the 5 V and ground runs short and appropriately sized.

Keep D+/D- inside proper short shielded USB 2.0 cable and retain known-good
USB-C host/OTG adapters for role detection. Before attaching either device,
verify upstream/downstream VBUS isolation, D+/D-/ground continuity, no shorts,
4.75–5.25 V downstream and zero backfeed into the P4-side VBUS. Test P4-only,
drive-only, FLX4-only and combined operation in that order. Use an oscilloscope
or a meter with a reliable min/max capture for startup and playback transients;
a normal multimeter may miss the short dip that causes a reset. Full evidence
and the resume gate are in
[`validation/P4_DUAL_USB_VBUS_BLOCKER_20260810.md`](validation/P4_DUAL_USB_VBUS_BLOCKER_20260810.md).
The subsequent runtime evidence is in
[`validation/P4_DUAL_USB_RUNTIME_SMOKE_20260812.md`](validation/P4_DUAL_USB_RUNTIME_SMOKE_20260812.md).

Externally powering the FLX4 can reduce the USB1 load, but it does not by
itself prove VBUS isolation or prevent backfeed. A powered USB hub may be used
only as a diagnostic aid because it changes the required direct-root topology.
Do not use a passive Y-cable or tie two independent regulated outputs together.

## Historical Inter-Board UART (Removed)

| Signal | ESP32-S3 | ESP32-P4 JC4880 JP1 | Direction |
| --- | --- | --- | --- |
| GND | GND | JP1 pin 3 or 4 | shared |
| 3.3 V | 3V3 | JP1 pin 1 or 16 | power, only if current budget is verified |
| UART TX | GPIO5 | GPIO28 / JP1 pin 19 | S3 -> P4 |
| UART RX | GPIO6 | GPIO29 / JP1 pin 12 | P4 -> S3 |

Leave the JC4880 UART0 connector free for flashing and diagnostics.

## Historical DDJ-FLX4 To S3 Path (Removed)

The DDJ-FLX4 connects to the S3 through USB. The S3 must run USB host mode and
enumerate the FLX4 as a USB MIDI device.

The historical XIAO wiring mapped the inter-board UART to GPIO5/GPIO6
instead of the previous DevKitC GPIO40/GPIO41 pair or the abandoned SuperMini
GPIO12/GPIO13 candidate.

Open wiring questions:

- the FLX4 is externally powered for the current XIAO bench setup; do not use
  the XIAO as the FLX4 operating-current supply;
- keep a valid host/OTG VBUS arrangement for enumeration without tying
  independent 5 V sources together;
- ensure common ground between S3, P4, audio boards, and any external supply;
- document actual VBUS/current behavior during bench testing.

## P4 Audio Outputs

The current Pajoniiir product topology uses two output paths:

- master output for speakers/PA/recording;
- cue/PFL output for headphones.

Implemented hardware plan:

| Output | Hardware | Notes |
| --- | --- | --- |
| Master | external PCM5102A I2S DAC | verified on GPIO50/GPIO52/GPIO51 through RCA and onboard 3.5 mm output |
| Cue/PFL | FLX4 USB headphones on P4 USB1 | P4 sends MAIN on UAC channels 1/2 and cue on 3/4 |

Headphones Mix DSP is implemented in the P4 output path: FLX4 Headphones Mix
raw `0` is cue/PFL, raw `16383` is master, and intermediate values blend cue
with stereo master. P4 sends that signal directly to the physical DDJ-FLX4
headphone jack through USB Audio Class. The
old onboard ES8311/speaker path is disabled in the product profile to free the
needed P4 I2S unit.

Important: do not short a BTL speaker amplifier output to ground. If the old
ES8311/speaker path is ever revived, inspect the schematic and bench-measure it
again before wiring headphones or line outputs.

## Historical Retired Inter-Board Pins

Inherited confirmed UART:

- P4 GPIO28: UART RX from S3 TX.
- P4 GPIO29: UART TX to S3 RX.

P4-to-S3 monitor PCM link for the FLX4 USB Audio headphones path
(DevKitC candidate hardware-validated 2026-07-02; XIAO ESP32S3/Sense wiring
validated 2026-07-06; see `docs/validation/FLX4_USB_AUDIO_E2E_SMOKE.md`):

| Signal | ESP32-P4 (JP1 pin) | ESP32-S3 side | Direction | Notes |
| --- | --- | --- | --- | --- |
| I2S BCLK | GPIO32 (JP1 pin 17) | GPIO7 | P4 -> S3 | clock for monitor PCM stream |
| I2S WS/LRCK | GPIO34 (JP1 pin 15) | GPIO8 | P4 -> S3 | stereo frame sync |
| I2S DOUT | GPIO35 (JP1 pin 13) | GPIO9 | P4 -> S3 | P4 `hp_out` monitor PCM data |
| GND | GND (JP1 pin 14) | GND | shared | required; use JP1 pin 14 next to the signal pins |
| READY/FLOW/debug | GPIO49 (JP1 pin 11) | not assigned | optional | not needed; leave disconnected |

The XIAO ESP32S3 migration set GPIO7/GPIO8/GPIO9 avoids the control UART
GPIO5/GPIO6 and UART0 GPIO43/GPIO44. The retired CDJ panel firmware no longer
claims these pins; the product S3 firmware always owns USB OTG as the FLX4 host.

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

**Product e2e result (validated 2026-07-07; rate-match re-smoke 2026-07-09):** with the XIAO GPIO7/GPIO8/GPIO9
link wiring, P4 `build_flx4_hp_e2e_tcmguard`, and S3
`build_flx4_hp_e2e_xiao`, the full path from P4 playback to the FLX4 headphone
jack was confirmed audible by the operator. P4 `MONITOR_PCM_LINK` counters rose
with `dropped=0`; S3 `P4_AUDIO_LINK` counters rose with `gaps=0` and `crc=0`
before the FLX4 USB Audio consumer was attached. A 2026-07-09 S3 product flash
fixed the remaining intermittent product-path overruns by keeping the FLX4 USB
Audio endpoint rate and packetizer synchronized to the active P4 link rate while
the ring stream is already running; the follow-up COM6 log held `overruns=0`
with `FLX4_USB_AUDIO skipped=0 underrun=0`. See
`docs/validation/FLX4_USB_AUDIO_E2E_SMOKE.md`.

**Product I2S unit budget (P4 rev v1.3 / eco2):** I2S unit 2 freezes on
`i2s_new_channel`, leaving units 0 and 1. Product config: **monitor link on
unit 0** (`CONFIG_MONITOR_PCM_LINK_I2S_UNIT=0`), **PCM5102A RCA MAIN on unit 1**,
and **ES8311 onboard monitor disabled** (`CONFIG_BSP_ES8311_MONITOR=n`) to free
unit 0. The FLX4 USB headphones are the CUE/MONITOR output; the local ES8311
monitor is dropped. This audio config is now the default (folded into each
board's `sdkconfig.defaults` on 2026-07-10), so a plain `idf.py build` has sound.

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
- ES8311/onboard monitor output is disabled in the current FLX4 USB headphones
  product profile; the P4-to-S3 monitor PCM link owns the CUE/MONITOR path.
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
