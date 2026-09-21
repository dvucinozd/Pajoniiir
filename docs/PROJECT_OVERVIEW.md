# Pajoniiir project overview

Status: **released P4-only product overview, reconciled 2026-09-20**.

Pajoniiir M2.1 is a standalone dual-deck DJ system. A Guition
`JC4880P443C_I_W` ESP32-P4 board hosts the Pioneer DDJ-FLX4 and Rekordbox USB
media directly, renders the 4.3-inch touch interface, owns playback/mixing/DSP
and exposes local Wi-Fi maintenance and signed OTA. A performance computer is
not required.

## Product topology

| Function | Active path |
| --- | --- |
| Music library | USB0 Rekordbox medium; FAT32/exFAT on superfloppy, MBR or GPT |
| Controller | USB1 DDJ-FLX4 MIDI IN/OUT |
| Headphone cue | DDJ-FLX4 UAC channels 3/4 |
| MAIN output | PCM5102A stereo RCA over P4 I2S |
| User interface | Local LVGL Overview, Library, Hot Cues and Settings |
| Network service | P4 Wi-Fi Remote, diagnostics, profile update and signed OTA |

The P4 is the sole authority for deck position, transport, mixer state, DSP,
controller binding and LED decisions. Compatibility naming inherited from the
former two-processor design does not represent a second runtime target.

## Released capabilities

- two independent MP3/WAV/FLAC decks with bounded seekable media caches;
- Rekordbox library, waveform, BPM, beatgrid and cue metadata;
- FLX4 transport, jog/vinyl/scratch, tempo, Master Tempo, mixer/EQ, PFL,
  Hot Cues, loops, Beat Jump/Sync, Pad FX and Beat FX;
- simultaneous PCM5102A MAIN and FLX4 headphone cue;
- controller reconnect handling, held-state release and LED resynchronization;
- signed local upload and newer-only public pull OTA with wired recovery;
- installable P4-local controller profiles, with FLX4 as the only physically
  qualified controller.

## Release state

Production release `M2.1` freezes commit `70824d24`. Its exact ESP-IDF v6.0.2
build, signed package, hardware installation, product smoke, public OTA channel
and GitHub Release passed. See
[`validation/M2_1_PRODUCTION_RELEASE_20260920.md`](validation/M2_1_PRODUCTION_RELEASE_20260920.md).

The current `master` may contain documentation or later development commits;
the tag remains the immutable production identity. Accepted security and
physical limitations are summarized in
[`DOCUMENTATION_STATUS.md`](DOCUMENTATION_STATUS.md) and governed by
[`SECURITY_PROVISIONING_POLICY.md`](SECURITY_PROVISIONING_POLICY.md).

## Start points

- Users: [`index.html`](index.html) and
  [`STARTUP_CHECKLIST.md`](STARTUP_CHECKLIST.md)
- Builders: [`HARDWARE_WIRING.md`](HARDWARE_WIRING.md)
- Developers: [`ARCHITECTURE.md`](ARCHITECTURE.md) and
  [`DEVELOPMENT_PLAN.md`](DEVELOPMENT_PLAN.md)
- Maintainers: [`OTA-UPDATE.md`](OTA-UPDATE.md),
  [`RISK_REGISTER.md`](RISK_REGISTER.md) and
  [`SECURITY_PROVISIONING_POLICY.md`](SECURITY_PROVISIONING_POLICY.md)

Superseded dual-processor plans and intermediate RC records are available in
Git history, not as active documentation.
