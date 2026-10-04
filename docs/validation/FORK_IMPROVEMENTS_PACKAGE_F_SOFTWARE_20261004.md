# Package F — UAC formats, MAIN routing and pacing

Status: **software verified; hardware acceptance NOT RUN**.
Production M2.4, public OTA channels, partitions and APTA remain unchanged.
Integration branch: `codex/fork-improvements`, following package E `9729de5`.
Implementation commit: `51ac8dab4ad22747740f9c6b26bedcefd48c2a46`, pushed and
verified against the remote branch. A subsequent documentation-only checkpoint
records CI and linker comparisons; it does not change firmware source.
Donor behavior reference: kayrozen/Pajoniiir
`428b97dd4a175f03d3a172c8db9c4d5ed94195fb` (v323). This adapts the existing
P4 UAC owner, ring and audio lifecycle instead of importing a second engine.
Existing source licenses remain intact. No donor attenuation diagnostic mode
or blanket IRAM relocation is adopted.

## Format and transport bounds

- Full-speed UAC1 Type-I PCM: 44,100 or 48,000 Hz, signed 16-bit in two bytes
  or packed 24-bit in three bytes. The runtime requires four channels:
  MAIN L/R on 1/2 and cue L/R on 3/4. Two-channel descriptors can be parsed
  but cannot provide the required runtime route.
- Generic devices require explicit PCM AS_GENERAL and adaptive/synchronous
  OUT, interval 1, sufficient packet size and the configured FIFO bound.
  Sampling-frequency SET_CUR is sent only when the class endpoint advertises
  that control. Exact FLX4 VID/PID preserves its established 44.1 kHz/16-bit
  selection and SET_INTERFACE/SET_CUR sequence.
- Feedback/implicit feedback, async OUT, high-speed, UAC2, other rates,
  24-in-32 and malformed/truncated descriptor tables are rejected. MIDI stays
  usable when audio is unsupported. No DDJ-400 VID/PID-specific audio quirk
  is asserted without descriptors from actual hardware.
- JC4880 preserves its 400-byte periodic packet bound and qualified FIFO
  allocation. JC1060 allows up to 640 bytes and uses 160 periodic FIFO lines
  on either root; a 48 kHz four-channel packed-24 packet occupies 576 bytes.
  JC1060 FIFO/USB topology still requires physical validation.
- The packer multiplies engine int16 samples by 256 before little-endian
  24-bit packing: full amplitude, defined arithmetic and bounded buffers.
  Generic USB MAIN has unity gain. Existing PCM-paced FLX4 cue attenuation
  remains unchanged; USB MAIN mode does not apply that mirror attenuation.

## Sink and lifecycle

JC4880 defaults to PCM5102A MAIN with I2S DMA pacing and existing USB cue mirror.
JC1060 defaults to USB MAIN/cue; PCM5102A and ES8311 are disabled. There is one
shared mixer/output producer. USB MAIN waits for real ring free space, bounded
to 100 ms per write and canceled by teardown/stream epoch changes. The ring
retains a 1,024-frame producer watermark in its 2,048-frame allocation.
USB mode uses ordinary writes, with no duplicate/trim or second software
audio clock. 44.1/48 kHz conversion remains bounded with preallocated stack
buffers. Playback position advances only for accepted MAIN output blocks;
an active USB failure takes the existing fail-closed transport path.

Sink changes require both decks stopped, no active LOAD/scratch, no recorder,
and an available destination. Admission/deck locks are tried without waiting
behind a LOAD. PLAY and scratch engage use the same deck lifecycle lock.
The output producer is joined before ring policy/epoch reset. Recorder START
from touch/Web uses the same admission boundary to prevent a START racing
a sink change. Recording remains disabled in normal builds; its experimental
writer, card latency and failure qualification belong to G.

Normal UAC teardown cancels/drains ISO ownership and sends alternate setting
zero before releasing the interface. An outstanding control request expires
after 1,000 ms, closes admission and requests controller-root recovery. Its
transfer allocation is retained until callback cancellation/detach; a timeout
never frees memory still owned by USB. Physical detach and EP0 recovery are
unrun hardware gates.

Both ordinary controller fault recovery and expired EP0 recovery use the board's
controller root (JC4880 root 1, JC1060 root 0); the historical fixed-root deferred
request is removed. Storage-root recovery is not substituted for controller reset.

## Compatible service API

The existing marked, Host-allowlisted POST `/api/control` accepts
`action=main_sink&value=usb` or `value=pcm5102a`, without a deck parameter.
Invalid values return 400; busy/unavailable destinations return 409.
Selection is session-local and returns to the board default after boot.
Existing actions retain their behavior. Settings touch presentation belongs to H.

GET `/api/status` adds optional `main_sink` and `uac_format` fields with
sample rate, bits, consumer pacing and `queued_us`. The latter is estimated
ring residence only: it excludes submitted USB packets, host scheduling and
DAC latency and must not be used as measured sink compensation for K.

## Verification

| Gate | Result |
| --- | --- |
| Full P4 host runner and static contracts | PASS |
| Audio lifecycle including active PLAY and in-flight LOAD sink rejection | PASS, 485 checks |
| Actual UAC stream compiled against USB/RTOS boundary stubs | PASS |
| Signed 24-bit full scale, distinct MAIN/cue bytes in actual ISO packet | PASS |
| 2,000 USB-paced writes and 2,000 44.1-to-48 kHz converted blocks | PASS, bounded sample count, no dup/trim |
| Consumer progress, full-ring timeout, cancellation, callback ownership, ALT0 and stalled EP0 | PASS |
| Legacy FLX4 format/gain, packet loss and producer/cleanup race regression | PASS |
| 300-second deterministic dual-deck Master Tempo soak | PASS, zero sample drift or clipping |
| Existing simulator navigation and 11 screenshots | PASS, baselines unchanged |
| ESP-IDF 6.0.2 JC4880 build | PASS, 2,545,568 bytes |
| ESP-IDF 6.0.2 JC1060 build | PASS, 2,542,912 bytes |
| Separate dependency locks, board identity, LVGL pin and 0x380000 budget | PASS |
| Diff/documentation checks | PASS |
| Hosted CI on `51ac8da` | PASS, host regression and both clean firmware jobs |
| Physical MAIN/cue listening, descriptor capture, reconnect, deadline/heap/stack measurement | NOT RUN |
| Installed image, 180-minute physical soak, signing, OTA/publication | NOT RUN |

Both targets were built in fresh `build_f` directories. JC1060 uses a fresh
SDKCONFIG inside that directory so inherited E bring-up ES8311 state cannot
override its USB-only defaults. One concurrent incremental reconfigure collided
on the local ComponentManager Git cache index lock; a sequential retry passed
without dependency changes. Both lock files remain unchanged.

[Hosted run 37197203523](https://github.com/dvucinozd/Pajoniiir/actions/runs/37197203523)
completed successfully on the exact implementation commit. Both container builds
passed lock stability, embedded project identity, BSP isolation, LVGL9.5.0,
binary budget and linked USB wrapper/resampler checks. The Linux host job includes
the actual UAC stream suite and simulator. No hardware or OTA action was run.

The local software checkpoint was built from E HEAD `9729de5` plus the F diff;
its embedded Git-derived version is `M2.4-42-g9729de5-dirty`. Commit-bound hosted
images are separate artifacts and must not be represented as this local binary.

| Local image | SHA-256 |
| --- | --- |
| JC4880 `build_f/main-deck-p4.bin` | `3ccbc0d102cd24281444393d589c036478b684be0bb926fcf9ef60fc28a43b07` |
| JC1060 `build_f/main-deck-jc1060.bin` | `26313136a34bc97289189714db43836841ad6096f26091acb7204b0f8b3fefa1` |

Run the normal host runner, `tests/controller_usb_audio/run_tests.ps1`,
`tests/audio_keylock_soak/run_audio_keylock_soak.ps1` and
`tests/ui_simulator/run_ui_simulator_e2e.ps1`. Build both IDF targets and run
`tools/check_board_build.py` for each image. CI runs the same board/lock/budget
checks. Runtime internal/DMA heap, PSRAM fragmentation, CPU deadlines and stack
headroom cannot be inferred from a host test or image size.

`esp_idf_size --format json2` on the local E2/F maps gives the following static
linker comparison. DIRAM used/free includes linker layout reservations; it is
not an available DMA heap measurement. External address-window size is not
reported as physical PSRAM capacity.

| Target | E2 DIRAM used/free | F DIRAM used/free | E2/F internal `.text` |
| --- | --- | --- | --- |
| JC4880 | 296,712 / 275,656 bytes | 296,744 / 275,624 bytes | 82,556 / 82,556 bytes |
| JC1060 | 294,077 / 278,291 bytes | 293,389 / 278,979 bytes | 80,960 / 80,312 bytes |

`controller_usb_audio_stream` is registered as a selectable functional suite in
the shared runner, so the actual stream owner executes on Windows and Linux CI,
not only via its standalone script. Fixed-rate generic 16-bit fixtures at both
rates also verify that unsupported sampling-frequency SET_CUR is not sent.

## Hardware acceptance and rollback

All following gates are NOT RUN: FLX4/PCM5102A regression listening and strict
audio/USB counters; DDJ-400 actual MIDI/audio descriptors, all four isolated
channels, sample level, restart/reconnect and listening soak; JC1060 render/
touch/storage under dual-deck load; final 180-minute soak on the exact candidate.
No configuration is newly hardware-accepted or production-ready.

Rollback is the existing signed project-compatible development/production image
or wired recovery. The release key, public channel and partition table were
not changed. No new image was flashed or published during this software work.
