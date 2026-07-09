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
  non-blocking `P4HP` blocks with sequence numbers and CRC32 over the protected
  header plus PCM payload.
- S3 `p4_audio_link` now accepts `P4HP` blocks, verifies the protected block
  CRC32 before sequence tracking, detects sequence gaps, and stores stereo
  frames in a 4096-frame ring.
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

## XIAO ESP32S3/Sense migration bench -- PASS WITH STABLE DELTA (2026-07-06)

Branch: `codex/s3-supermini-migration`

The control board was migrated from the earlier ESP32-S3 DevKitC candidate to
the Seeed Studio XIAO ESP32S3 / XIAO ESP32S3 Sense. The XIAO does not expose
GPIO15/GPIO16/GPIO17 as the preferred audio-link group in this harness, so the
P4-to-S3 monitor PCM link moved to XIAO GPIO7/GPIO8/GPIO9.

Bench build profiles:

- P4: `sdkconfig.monitor_link_bench` built into `build_monitor_link_bench_fixed`.
  The bench profile starts only the monitor PCM transport/tone generator and
  skips full P4 app startup.
- S3: `sdkconfig.p4_link_bench` built into `build_p4_link_bench_diag`.
  The bench consumer includes raw I2S RX counters while
  `CONFIG_P4_AUDIO_LINK_BENCH_CONSUMER=y`.

Wiring validated on hardware:

| Signal | P4 GPIO | P4 JP1 pin | XIAO ESP32S3/Sense pin |
| --- | --- | --- | --- |
| I2S BCLK | GPIO32 | JP1 pin 17 | D8 / GPIO7 |
| I2S WS/LRCK | GPIO34 | JP1 pin 15 | D9 / GPIO8 |
| I2S DOUT | GPIO35 | JP1 pin 13 | D10 / GPIO9 |
| GND | GND | JP1 pin 14 | GND |

60-second confirmation capture after flashing the diagnostic S3 bench:

| Metric | Measured |
| --- | --- |
| S3 received blocks | `10272` by the captured endpoint |
| S3 sequence gaps | `0` |
| S3 CRC errors | `0` |
| S3 underruns / overruns | `0 / 0` |
| Raw I2S reads | `5136` |
| Raw I2S bytes | `10518528` |
| Raw reads with non-zero data | `5136` |
| Raw reads containing `P4HP` magic | `5066` |
| Raw I2S timeouts / errors | `0 / 0` |

Initial 5-minute soak follow-up before the block-CRC hardening:

| Metric | Measured |
| --- | --- |
| P4 TX last counter | `tx submitted=198241 dropped=12 sent=198240` |
| S3 last RX counter | `rx blocks=113174 gaps=1 crc=0 ring=2048 underruns=0 overruns=0` |
| S3 last raw counter | `raw reads=56591 bytes=115898368 nonzero=56587 magic=55821 timeouts=0 errors=0` |

The initial 5-minute soak confirmed that the physical XIAO wiring and I2S RX
path were healthy: raw data arrived continuously, `P4HP` magic was found, CRC
stayed zero, and there were no I2S read timeouts/errors or ring
underruns/overruns. It also showed that payload-only CRC left one ambiguity:
an altered header could reach sequence tracking and appear as a sequence gap
without a CRC error.

A subsequent 120-second S3-only delta check, still before block-CRC hardening,
did not add further link-quality errors:

| Metric | Start | End |
| --- | --- | --- |
| S3 RX | `rx blocks=120930 gaps=2 crc=0 ring=2048 underruns=0 overruns=0` | `rx blocks=150726 gaps=2 crc=0 ring=2048 underruns=0 overruns=0` |
| S3 raw | `raw reads=60473 bytes=123848704 nonzero=60466 magic=59647 timeouts=0 errors=0` | `raw reads=75371 bytes=154359808 nonzero=75364 magic=74343 timeouts=0 errors=0` |

Block-CRC follow-up:

- `monitor_pcm_link` / `p4_audio_link` now protect the whole `P4HP` block:
  header with the CRC field cleared, plus PCM payload.
- Host regression coverage rejects a header-corrupted block before sequence
  tracking, so header corruption cannot be misreported as a clean sequence gap.
- A combined P4+S3 serial capture showed `gaps=1 crc=1` only while opening
  COM15 reset the P4 during S3 reception; this is a startup/reset disturbance,
  not a steady-state link delta.
- Stable S3-only 180-second delta, with P4 left running and COM15 untouched:
  `rx blocks +44836`, `gaps +0`, `crc +0`, `timeouts +0`, `errors +0`.
- Stable S3-only 300-second delta, with P4 left running and COM15 untouched:
  `rx blocks +74864`, `gaps +0`, `crc +0`, `ring +0`,
  `underruns +0`, `overruns +0`; raw I2S `reads +37432`,
  `bytes +76660736`, `nonzero +37432`, `magic +37432`,
  `timeouts +0`, `errors +0`.

Follow-up before product integration changes:

- repeat this zero-delta soak after any I2S, task-priority, or FLX4 USB Audio
  scheduling change;
- keep the XIAO GPIO7/GPIO8/GPIO9 wiring as the confirmed physical pinout for
  further FLX4 USB headphones integration work.

## Product USB Audio scheduling follow-up -- PASS (2026-07-09)

The 2026-07-09 S3 product fix changed FLX4 USB Audio scheduling logic, not the
I2S wire format or `P4HP` block format. The required follow-up was therefore a
product-path counter check with P4 playback active and the FLX4 USB headphone
consumer draining the S3 ring.

Observed on S3 `COM6` after flashing `build_audio_20260709_s3`:

| Metric | Observed |
| --- | --- |
| S3 received blocks | increased steadily (`47` -> `21949` in the captured window) |
| P4 sequence gaps | `0` |
| S3 CRC errors | `0` |
| S3 overruns | `0` |
| FLX4 USB skipped / underrun packets | `0 / 0` |
| Ring fill | stable below the 4096-frame ceiling instead of pinned full |

Result: the FLX4 USB Audio scheduling change did not introduce link corruption,
and it removed the product-path ring overrun symptom caused by the previous
post-start sample-rate mismatch.
