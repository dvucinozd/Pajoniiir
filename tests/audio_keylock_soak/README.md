# Dual-deck Master Tempo host soak

This deterministic host test runs two real `audio_keylock.c` instances
simultaneously:

- Deck 1: 48 kHz source, Master Tempo factor `1.15`;
- Deck 2: 44.1 kHz source converted to 48 kHz, Master Tempo factor `0.85`.

Both sources are seamless generated stereo PCM fixtures. The test checks:

- exact output-frame duration and bounded source-position drift;
- approximate pitch preservation from positive zero crossings;
- finite long-running DSP coordinates across periodic rebasing;
- sample discontinuities above the click threshold;
- per-deck and unsaturated dual-deck clipping;
- successful rendering for every requested frame.

Run the five-minute virtual soak from the repository root:

```powershell
.\tests\audio_keylock_soak\run_audio_keylock_soak.ps1
```

Choose another virtual duration when needed:

```powershell
.\tests\audio_keylock_soak\run_audio_keylock_soak.ps1 -VirtualSeconds 900
```

The test runs offline faster than real time. Host CPU time is diagnostic only:
it does not represent ESP32-P4 scheduling margin, I2S deadlines, PSRAM/media
contention or listening quality. Those remain hardware acceptance items.
