# P4 dual Master Tempo WDT remediation — 2026-09-19

Status: **reproduced, source-fixed and bounded exact-image hardware smoke PASS;
long combined-load soak remains open**.

## Reproduction

The signed `RC2-149-ga9c2898` image reproduced an audible interruption and
Task-WDT reset with both decks running Master Tempo at different pitch and
sample-rate conditions. The retained record identified:

- `IDLE0` starvation on CPU0;
- current task `ae_output`;
- PC/RA in `audio_keylock_next()`;
- WDT phase `mix_group`, both decks active;
- output-late events up to `17,836 us`, an 815-frame PCM underrun and active
  `UAC_DATA_LOSS` before the reset.

The attempted two-tick coordinated idle window was therefore rejected. It did
not guarantee that IDLE0 ran, and sleeping the output task created an audible
deadline hole.

## Root cause and source change

Hardware evidence showed that dual keylock work exceeded the roughly 5.8 ms
output-block period. The old WSOLA grain search exhaustively evaluated every
candidate with 16 reference samples, repeatedly reading the same canonical
PCM from PSRAM.

Commit `909e068567667f832142088642eef24f28ace87c` replaces that hot path with:

- a bounded coarse-to-fine search that still covers the complete radius;
- native 32-bit stereo SAD and early losing-candidate rejection;
- a fixed per-search cache for repeated canonical PCM reads;
- an integrated logical source clock so correlation offsets cannot cancel
  small tempo changes;
- removal of the failed two-tick output/decoder scheduler workaround.

No PAJONIIIR-M3 BSP, USB topology or board assumption was imported. Only the
portable keylock search strategy and its test approach were adapted.

## Host and build evidence

- Complete `tests/run_p4_host_tests.ps1`: PASS.
- Search regression: at most 30 candidates and 133 source reads per rendered
  frame across tested ratios from 0.25 to 4.0; enforced limits are 40 candidates
  and a ratio-derived read budget.
- Acoustic tempo-response regression: 44.1/48 kHz combinations and tempo
  factors 0.8 through 1.2 passed; worst detected onset error was `2.625 ms`
  against the `15 ms` limit.
- ESP-IDF: v6.0.2.
- Clean signed version: `RC2-150-g909e068`.
- Application size: `2,459,008` bytes.
- Application SHA-256:
  `255b4a19142bdfe57a6a4691cca73f43fe671b28120a9894b85f7a58bbc88177`.
- Signed bundle verification: PASS, ECDSA P-256 key `rel-001`.

## Exact-image hardware smoke

The signed image installed on `ota_1`. USB0 mounted, the Library published 324
tracks, and USB1 restored the exact FLX4 profile with MIDI IN/OUT and USB audio.

Fixture:

- Deck 1: `TAINTED DUB - CLIP.mp3`, raw pitch `10500` (`-2.81%`);
- Deck 2: `Sample_BeeMoved_96kHz24bit.flac`, 96 kHz/24-bit, raw pitch `6000`
  (`+2.67%`);
- both decks playing four-beat loops with Master Tempo enabled physically.

The operator confirmed that both Master Tempo decks sounded normal. A
subsequent 187-second monitored window exceeded the failed image's reset point
while the boot epoch remained 8 and both decks continued playing. Final
telemetry was:

- current TWDT ISR: false;
- PCM underruns: `0 / 0`;
- UAC `data_loss=false`, flags `0`, packet failures `0`;
- 12 isolated output-late warnings, maximum `15,569 us`, without an underrun,
  UAC loss, audible failure or reboot;
- internal free heap `54,531` bytes; PSRAM free `24,882,300` bytes.

## Acceptance boundary

This closes the reproduced immediate dual-Master-Tempo WDT failure and rejects
the failed scheduler workaround. It does not close the P1 sustained combined
DSP risk or Phase 5. A declared longer run must still combine Master Tempo,
mixed formats, pitch changes, seek/scratch/FX and MAIN/cue listening while
tracking phase maxima, late-event rate, underruns, UAC loss and boot epoch.

## Follow-up image and lifecycle/search findings

The next signed working-tree image, `RC2-151-g838c254-dirty`, additionally
removes output-owner 64-bit timeline seqlock work from the per-sample keylock
path and reduces the bounded WSOLA reference work. The full P4 host suite and
ESP-IDF v6.0.2 `build_signed` passed. Installed `ota_0` image:

- size: `2,459,136` bytes;
- SHA-256: `a96e78e9fb498decd7820846c1105221d21b4989bc4e733280b0d00496be40b0`;
- signed bundle: ECDSA P-256, key `rel-001`;
- live USB0 mounted, FLX4 profile/MIDI/UAC active.

The exact D1 MP3 plus D2 96 kHz/24-bit FLAC fixture, both Master Tempo with
opposing approximately 3% pitch, ran for 184 seconds with PCM `0/0`, zero UAC
loss, no WDT and maximum observed mix phase `7,524 us`. The operator confirmed
continuous clean sound. This is a focused smoke, not the combined release gate.

During the first functional attempt, near-EOF Shift+Jog search while a four-beat
loop remained active reproduced D2 falsely reporting `PLAYING` while its
position stayed frozen and a tiny segment repeated. Reload cleared the state.
The fix makes explicit jog search exit the active loop and clamps it to
`duration_ms - 1`; its regression failed before the fix and passes in both
ordinary and scratch-enabled deck-core suites. Post-OTA hardware retest reached
natural EOF normally and restarted with PCM/locked-read/UAC counters at zero.

A subsequent declared 45-minute functional run stopped at Stage 2 after about
16.8 minutes with D1 `+165` PCM underrun frames and USB-headphone `+47,234`
underflow frames. Both decks remained PLAYING; there was no UAC data-loss flag,
packet failure, WDT, reboot or locked read, and the operator did not confirm an
audible defect. Isolated Hot Cue, scratch and search repetitions on each deck
did not reproduce the PCM/USB growth. The run remains a FAIL; Stage 2 is now
specified deck-by-deck for a deterministic rerun, with the strict counter gates
unchanged.

The deterministic rerun passed Stage 1 and the deck-by-deck Stage 2 actions,
then failed at about 20.5 minutes with `+15,510` FLX4 headphone-underflow
frames (about 352 ms at 44.1 kHz) and active UAC loss flag `0x10`. Deck PCM
underruns, locked reads, packet failures and dropped UAC blocks remained zero;
both decks stayed PLAYING, the boot epoch did not change and the operator did
not hear an interruption. The retained record is
`tmp/p4-release-qualification/functional-20260919T201450Z-boot444.json`, with
diagnostic event `seq=39`, `ms=3180390`, `event=UAC_DATA_LOSS`, `a2=15510`.
This is still a strict release-gate FAIL.

Source inspection found the UAC consumer task raised from priority 5 to 7 while
streaming, above its sole ring producer `ae_output` at priority 6. Dense jog
MIDI traffic can therefore keep the consumer runnable while preempting the
producer, matching the observed healthy deck PCM and empty UAC ring. The next
candidate image sets both tasks to priority 6. ESP-IDF 6.0.2 has time slicing
enabled, and the output task already blocks on I2S and explicitly yields, so
equal priority preserves ISO service without enlarging the 2,048-frame ring or
adding cue latency.

## Equal-priority focused hardware verification

The equal-priority candidate was built with ESP-IDF v6.0.2, packaged and
verified with ECDSA P-256 key `rel-001`, then installed by signed push OTA on
`ota_1`. The application remained 2,459,136 bytes with SHA-256
`f1da53d71081bc0fb77a7b3970996307d3858a6ac8fd20287295508decf23322`.
Post-boot status confirmed USB0 mounted, 324 tracks published, the exact FLX4
profile active and MIDI IN/OUT plus UAC healthy.

Fixture:

- D1 `TAINTED DUB - CLIP.mp3`;
- D2 `Sample_BeeMoved_96kHz24bit.flac` (96 kHz/24-bit);
- both decks in four-beat loops with Master Tempo and opposing approximately
  3% pitch;
- MAIN and cue were audible and the operator confirmed normal sound.

The operator exercised D1 and then D2 with rapid jog traffic, repeated scratch
release/re-grab, Hot Cue and Shift+Jog search while the other deck continued.
This produced 2,658 new MIDI packets. Across the complete physical action
window the deltas were UAC underflow `0`, overflow `0`, dropped blocks `0`, PCM
`0/0`, locked reads `0/0`, packet failures/lost frames `0/0` and output-late
events `0`; both deck passes retained a nominal UAC ring and no WDT or reboot.

After both loops were re-armed, a separate strict ten-minute stabilization
window also completed with UAC underflow/overflow `0/0`, PCM `0/0`, no dropped
blocks, no output-late event and a final nominal ring fill of 1,056/2,048
frames. The operator confirmed the entire ten-minute window was free of audible
interruptions and artifacts.

The monotonic UAC underflow counter did increase while the automated CUE/restart
transition re-armed transport, but the playback-session loss flag stayed clear,
no `UAC_DATA_LOSS` service event was emitted on this boot, and the counter was
stable at zero delta throughout each declared active-playback window. This is
not counted as active playback acceptance evidence.

The focused scheduler remediation is therefore **hardware PASS**. The earlier
45-minute functional run remains a correctly recorded FAIL; a new complete
functional gate is still required for release closure.
