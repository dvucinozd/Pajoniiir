# RC2-116 limiter/WDT OTA and three-hour soak

Date: **2026-09-10**

Status: **PASS for the targeted limiter-telemetry watchdog regression**. This
record does not close the full combined-load, USB lifecycle, electrical or
closed-enclosure release gates.

## Exact image

- Branch: `feat/p4-dual-usb-host`
- Commit: `77d723c8d19b1a859b9f5b4fa8421928250c68d3`
- Version: `RC2-116-g77d723c`
- Toolchain: ESP-IDF v6.0.2
- Installed slot: `ota_1`
- OTA state after reboot: `idle`, empty `last_error`
- Application size: `2,452,672` bytes
- Application SHA-256:
  `ead88e980b12c06be8f8655cd1018a5671525f07c4c4a5e21b58cfb303a62d19`
- Signed bundle size: `2,452,860` bytes
- Signed bundle SHA-256:
  `753b22ca2f3276786897f8fd48401fd9c397084489bfdbcc9eb808452170fb3d`
- Bundle:
  `releases/pajoniiir-RC2-116-g77d723c/main-deck-p4.ddjota`

Before packaging, the complete P4 host suite passed, including 401 audio-engine
checks and the deterministic preempted-writer regression. A clean isolated
`build_signed` build and signed-package verification also passed. The package
was then accepted by the device over signed OTA with HTTP 200 and the exact
version was observed after reboot.

## Test conditions

The device was power-cycled after installation and stayed on boot epoch `389`
for the complete run. USB0 remained mounted and the DDJ-FLX4 remained connected
with its profile, MIDI IN, MIDI OUT and UAC interfaces active.

Both decks continuously looped MP3 material:

- Deck 1: Bon Jovi, `Bad Medicine`
- Deck 2: Jonas Brothers, `Burnin' Up`

The last captured service-log event was at `11,050,522 ms`, approximately
3 hours 4 minutes after boot. The run intentionally exercised continuous
dual-deck playback and limiter bookkeeping. It did not include the full planned
seek, scratch, FX, hotplug, web-traffic or mixed-format stress matrix.

## End-state evidence

| Signal | Result |
| --- | --- |
| Boot epoch | `389`, unchanged throughout the run |
| New panic, brownout or task-WDT reset | none |
| `twdt_isr_seen` | `false` |
| PCM underruns | Deck 1 `0`, Deck 2 `0` |
| UAC submitted blocks | `1,999,090` |
| UAC dropped blocks | `0` |
| UAC overflow frames | `0` |
| UAC packet failures / lost frames | `0` / `0` |
| Active UAC `data_loss` | `false` |
| Controller disconnects / queue failures | `0` / `0` |
| USB host daemon errors / service-log drops | `0` / `0` |
| Limiter samples | `0`; peak input magnitude `25,012` |
| PSRAM trend in final 30 minutes | unchanged |
| Internal heap trend in final 30 minutes | `-416` bytes; insufficient evidence of a leak |

The lifetime UAC underflow counter was `833,137`, but it did not increase in the
final 30-minute window and the playback-session loss flag remained clear. It is
therefore retained as idle/startup telemetry, not classified as active playback
loss.

The crash-dump endpoint still exposed an older `RC2-114` `esp_timer` dump. Its
identity was unchanged and there was no corresponding boot change or new dump
during this run, so it is historical evidence rather than a failure of this
image. The dump was deliberately not cleared.

## `AUDIO_OUTPUT_LATE` analysis

Fourteen late-block warnings were recorded over `1,999,090` submitted UAC
blocks, approximately `0.00070%` of blocks. The maximum block time was
`12,169 us`; at 44.1 kHz the warning threshold is `11,610 us`, so the worst
sample exceeded the deliberately sensitive two-block threshold by `559 us`
(`4.8%`). Ten warnings occurred in the first approximately 2.5 hours and four
in the final approximately 30 minutes. No warning coincided with an underrun,
UAC loss flag, packet failure, observable output failure reported during the
run, or reboot.

Source inspection confirms:

- the warning threshold is twice the 256-frame block period;
- the measured block includes rendering, USB cue submission, the blocking
  PCM5102A I2S write, bookkeeping and scheduler preemption;
- the periodic fairness yield runs after timing is sampled and cannot create
  these warnings;
- the 50 ms `AUDIO_BLOCK_OUTLIER` threshold was never reached;
- phase lifetime maxima were: head `1,124 us`, mix `6,890 us`, push `570 us`,
  monitor `1,379 us`, MAIN I2S `10,501 us`, codec `741 us`, bookkeeping
  `907 us`.

Those phase maxima are independent lifetime maxima, not a same-event trace, so
they cannot prove the precise cause of any individual warning. They do show
that the blocking MAIN I2S pacing path, plus occasional scheduling jitter, is
the plausible dominant cost. The evidence does not support a limiter deadlock,
DSP overload or data-loss defect, and no firmware change is justified from
these warnings alone.

The engine snapshot contains main-sink call, short-write, timeout, error and
failed-block counters, but `/api/status` does not currently serialize them.
Consequently this run does not claim a measured zero for those internal
counters; it claims only that no output failure was observed and that all
exposed downstream-loss counters remained healthy. This is an observability
gap to close before the strict final timing/sink gate.

For the later worst-case timing gate, capture/reset phase and sink counters at
the start of a declared combined-load run and correlate any new warning with
scratch, Master Tempo, FX and web activity. Treat a reproducible deadline miss,
sink error, underrun, active UAC loss or audible defect as the trigger for a
code change; do not optimize from this warning count alone.

## Disposition

The `77d723c` limiter telemetry fix is hardware-confirmed for a targeted
three-hour continuous dual-MP3 loop. The prior watchdog-reset symptom did not
recur. The broader Phase 8 combined-load soak remains open because this run did
not exercise the complete declared stress mix or controlled USB reconnects.
