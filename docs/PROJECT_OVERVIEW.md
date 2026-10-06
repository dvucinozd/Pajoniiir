# Pajoniiir project overview

Status: **published JC4880/FLX4 M2.5; shared-core development extensions**
(2026-10-06).

Pajoniiir is a standalone dual-deck DJ system. The JC4880P443C_I_W ESP32-P4
hosts Pioneer DDJ-FLX4 and Rekordbox USB media directly, renders the 4.3-inch
touch interface and owns playback, mixing, DSP, local Wi-Fi service and signed
OTA. No performance computer or secondary playback processor is required.

## Published configuration

| Function | JC4880 / FLX4 path |
| --- | --- |
| Music library | USB0 Rekordbox medium; FAT32/exFAT on superfloppy, MBR or GPT |
| Controller | USB1 DDJ-FLX4 MIDI IN/OUT |
| MAIN | PCM5102A stereo RCA over P4 I2S |
| Headphone cue | FLX4 UAC channels 3/4 |
| UI | Previous LVGL design: Overview, Library, Hot Cues, Settings; native 800x480 |
| Service | Wi-Fi Remote, resources/diagnostics, profiles and signed OTA |

Current release **M2.5** freezes source
`20f1c3f04a615209ae25e9bbdae649d0f5b44e8d` on ESP-IDF v6.0.2.
[GitHub](https://github.com/dvucinozd/Pajoniiir/releases/tag/M2.5)
and the [public OTA channel](https://ota.pajoniiir.eu/latest.json) publish the
original tested artifacts. Later `master` commits do not change that image.

M2.5 adds the Pajoniiir artwork fallback and passed final tagged OTA/startup
and 62.622211 seconds of operator-confirmed clean dual playback. A new long
soak was explicitly waived because audio code is unchanged; see the
[M2.5 record](validation/M2_5_RELEASE_20261006.md). Older v91 scenarios below
remain evidence for their original image.

## Implemented capabilities

- MP3, PCM16 RIFF/WAV and FLAC playback with bounded seekable caches and
  accepted-audio timeline; WAV float/24-bit/extensible formats remain unsupported.
- Rekordbox title/key/BPM/beatgrid, ordered playlists, artwork, PWAV/PWV3/PWV4,
  hot cues and separate bounded memory cues.
- FLX4 transport, jog/vinyl/scratch, pitch/Master Tempo, mixer/EQ, PFL,
  cue/loop/Beat Jump/Sync, Pad FX and Beat FX.
- Persistent local cue overrides/deletions and explicit source restore;
  default-on load lock.
- MAIN plus headphone cue, USB lifecycle handling and state/LED resynchronization.
- S3CP v2/v3/v4 data-driven profiles, atomic guarded upload and binding-epoch
  validation.
- Signed local OTA and newer-only public pull, project/version/hash checks,
  bounded startup confirmation and rollback.

Implemented features do not imply every combination was physically tested.
The historical [v91 record](validation/JC4880_V91_RELEASE_20261005.md) lists focused
audio/transport/cue/reconnect checks and the operator-accepted segmented
180m01.37s soak exception. Uninterrupted 180-minute qualification, exhaustive
metadata/touch variants and other unrun scenarios remain NOT RUN.

## Shared-core development configurations

A-L integration is merged into `master`. Modules, fixes and ideas were adapted
from collaborator [kayrozen](https://github.com/kayrozen), frozen donor
`428b97dd`; see [integration/provenance](FORK_IMPROVEMENTS.md).

The separate `main-deck-jc1060` entrypoint shares the same P4 engine and adds
native 1024x600 display/touch configuration, DDJ-400 profile, USB MAIN/cue and
default-off Ethernet-only Pro DJ Link discovery, browse, verified SD download
and network sync. Builds/mock tests are software verification only.
JC1060/DDJ-400/real CDJ or rekordbox peers remain **hardware NOT RUN**.
JC4880 has no Link transport. There is no local browsable Link server.

Recorder, SD experiments and UI preview remain optional development features,
disabled in regular releases. APTA stays separate and is not integrated.
`S3CP`, `profile.s3bin` and `control_link` are compatibility names;
no S3 firmware or UART/PCM bridge is active.

## Documentation

- Operation: [manual](index.html), [checklist](STARTUP_CHECKLIST.md),
  [wiring](HARDWARE_WIRING.md).
- Development: [architecture](ARCHITECTURE.md), [plan](DEVELOPMENT_PLAN.md),
  [profiles](CONTROLLER_PROFILE_SCHEMA.md).
- Maintenance: [current status](DOCUMENTATION_STATUS.md), [OTA](OTA-UPDATE.md),
  [risks](RISK_REGISTER.md), [security](SECURITY_PROVISIONING_POLICY.md).
- Evidence and history: [documentation index](README.md).
