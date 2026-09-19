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
