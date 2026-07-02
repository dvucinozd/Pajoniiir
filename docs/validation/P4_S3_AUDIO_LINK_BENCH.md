# P4 to S3 Monitor PCM Link Bench

Date: 2026-07-02
Branch: `codex/flx4-usb-audio-headphones`

## Decision for first bench pass

The first dedicated P4-to-S3 monitor PCM transport candidate is I2S:

| Signal | P4 pin | S3 pin | Direction | Status |
| --- | --- | --- | --- | --- |
| I2S BCLK | GPIO32 | GPIO15 | P4 -> S3 | selected for bench |
| I2S WS/LRCK | GPIO34 | GPIO16 | P4 -> S3 | selected for bench |
| I2S DOUT | GPIO35 | GPIO17 | P4 -> S3 | selected for bench |
| READY/FLOW/debug | GPIO49 | GPIO18 | optional | reserved; not required for first bench |
| GND | GND | GND | shared | required |

Rationale:

- I2S matches the payload shape: stereo 16-bit PCM frames from P4 `hp_out`.
- The existing `0xA5` control UART remains semantic/control-only and is not used
  for PCM payload.
- The S3 GPIO15-GPIO18 candidate set is valid only while
  `CONFIG_DDJ_FLX4_HOST_MODE=y` keeps legacy `panel_io` inactive.
- GPIO48 remains free for later LED work.

## Implemented software slice

- P4 `monitor_pcm_link` now serializes enabled monitor PCM writes into
  non-blocking `P4HP` blocks with sequence numbers and payload CRC32.
- S3 `p4_audio_link` now accepts `P4HP` blocks, verifies CRC32, detects sequence
  gaps, and stores stereo frames in a 4096-frame ring.
- Ring overrun drops the oldest monitor frames.
- Ring underrun returns silence and increments diagnostics.

## Pending hardware bench

This document does not yet record a physical 60-second I2S bench result.

Required bench acceptance before enabling the runtime transport by default:

| Metric | Required result |
| --- | --- |
| P4 sequence gaps | `0` at steady state |
| S3 CRC errors | `0` |
| S3 underruns | `0` after initial startup |
| S3 overruns | `0` at steady state |
| FLX4 MIDI responsiveness | Play/Pause, Browse, LEDs remain responsive |
| S3/P4 stability | no reboot |

Do not proceed to product end-to-end USB headphone streaming until this bench
pass is recorded here.
