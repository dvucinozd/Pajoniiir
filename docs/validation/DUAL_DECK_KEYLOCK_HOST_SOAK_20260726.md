# Dual-deck Master Tempo host soak — 2026-07-26

## Scope

The deterministic host soak runs two `audio_keylock.c` instances concurrently
against seamless generated stereo PCM fixtures:

- Deck 1: 48 kHz source, 48 kHz output, tempo factor `1.15`;
- Deck 2: 44.1 kHz source, 48 kHz output, tempo factor `0.85`.

The standalone five-minute run renders 14,400,000 output frames per deck. It
checks source-position drift, approximate pitch preservation, finite DSP state
across coordinate rebasing, sample jumps above 6,000, per-deck clipping and
unsaturated dual-deck clipping.

Command:

```powershell
.\tests\audio_keylock_soak\run_audio_keylock_soak.ps1 -VirtualSeconds 300
```

## Result

| Metric | Deck 1 | Deck 2 |
| --- | ---: | ---: |
| Output frames | 14,400,000 | 14,400,000 |
| Consumed source frames | 16,559,999 | 11,245,499 |
| Source-position drift | 0.0 frames | 0.0 frames |
| Measured fixture frequency | 368.983 Hz | 407.970 Hz |
| Frequency error | 0.156% | 0.119% |
| Peak sample | 9,088 | 9,665 |
| Maximum adjacent-sample jump | 570 | 614 |
| Detected clicks | 0 | 0 |
| Clipped samples | 0 | 0 |

The unsaturated dual-deck sum peaked at 18,752 with zero clipped samples.
Periodic checks found finite key-lock coordinates throughout the run, and both
states rebased their long-running float working coordinates while retaining
the exact 64-bit source position. Host CPU time was 2.775 seconds and is
reported only to make the run reproducible.

## Gate integration and boundary

`tests/run_p4_host_tests.ps1` builds the same executable and runs a five-second
variant on every normal P4 host gate. The standalone runner defaults to the
five-minute virtual duration and accepts up to one hour.

This test catches deterministic key-lock drift, pitch, discontinuity, state and
clipping regressions. It does not model ESP32-P4 task scheduling, I2S deadlines,
PSRAM/media contention, enclosure conditions or subjective listening quality.
Those remain hardware acceptance items.
