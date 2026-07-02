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

## Hardware bench result — PASS (2026-07-02)

Bench build profiles:

- P4: `sdkconfig.monitor_link_bench` (`CONFIG_MONITOR_PCM_LINK_ENABLED=y`,
  `CONFIG_MONITOR_PCM_LINK_BENCH_TONE=y`), built into `build_monitor_link_bench`.
- S3: `sdkconfig.p4_link_bench` (`CONFIG_P4_AUDIO_LINK_ENABLED=y`,
  `CONFIG_P4_AUDIO_LINK_BENCH_CONSUMER=y`), built into `build_p4_link_bench`.

Wiring used for the passing run (JP1 pin numbers from
`docs/board-jc4880p443c-i-w-analysis.md`):

| Signal | P4 GPIO | P4 JP1 pin | S3 pin |
| --- | --- | --- | --- |
| I2S BCLK | GPIO32 | JP1 pin 17 | GPIO15 |
| I2S WS/LRCK | GPIO34 | JP1 pin 15 | GPIO16 |
| I2S DOUT | GPIO35 | JP1 pin 13 | GPIO17 |
| GND | GND | JP1 pin 14 (+ a second GND) | GND (two used) |

Measured over an 80-second continuous capture with fresh S3 counters:

| Metric | Required | Measured |
| --- | --- | --- |
| S3 received blocks | steady stream | `19789` over 79 s (~250 blocks/s) |
| P4 sequence gaps | `0` at steady state | `0` |
| S3 CRC errors | `0` | `0` |
| S3 underruns | `0` after startup | `0` |
| S3 overruns | `0` at steady state | `0` (ring stable at 2048/4096) |
| P4 dropped blocks | `0` at steady state | `0` (`sent=32796`, `dropped=0`) |
| FLX4 MIDI responsiveness | Play/Pause, Browse, LEDs responsive | confirmed responsive, no stutter |
| S3/P4 stability | no reboot | no reboot on either board |

Block rate matches 48 kHz stereo 16-bit monitor audio: 250 blocks/s x 192
frames/block = 48000 frames/s.

## Findings that shaped the working transport

- **I2S pipe rate:** the first attempt at 96 kHz 16-bit stereo slots
  (3.072 MHz BCLK) showed ~90% CRC corruption over the jumper harness. The
  bench runs at 64 kHz slots (2.048 MHz BCLK, ~1.3x the framed-stream payload
  rate) and must match on both boards
  (`MONITOR_PCM_LINK_I2S_PIPE_RATE_HZ` / `P4_AUDIO_LINK_I2S_PIPE_RATE_HZ`).
- **Root cause of block loss was software, not wiring:** the ESP32 I2S TX DMA
  is a continuously-transmitting circular buffer. When the writer slept
  between blocks, the DMA lapped it and `auto_clear` zeros went out mid-block.
  The fix keeps the transport task writing at line rate — real blocks when the
  queue has data, explicit zero filler (which the deframer skips) otherwise.
- **I2S unit budget (rev v1.3 / eco2 silicon):** acquiring I2S unit 2 hard-froze
  the P4 during `i2s_new_channel`, and `I2S_NUM_AUTO` stole the ES8311 unit 0.
  The transport is pinned to I2S unit 1, and the bench profile keeps PCM5102A
  MAIN OUT off to leave unit 1 free. **Task 8 product integration must resolve
  the final unit budget** (ES8311 monitor vs. FLX4 USB monitor link on one
  unit), since the product path needs both ES8311/PCM5102A and the link.
- **S3 RX robustness:** 8 x 500-frame RX DMA descriptors (~64 ms cushion) plus
  a streaming deframer that CRC-checks each `P4HP` block and resyncs on the
  magic keep USB-host bursts from corrupting the ring.
