# JC4880 / FLX4 v91 release-candidate review

Status: **prepared for review; not publicly released** (2026-10-05).

## Frozen identity and retained artifacts

- Firmware/source: `M2.4-91-g75136aef`, `75136aef749a1f03d9089c8b6ff6452b3dba0839`.
- Installed product: JC4880 / FLX4 / PCM5102A MAIN / FLX4 cue, `ota_1`.
- Image: 2,570,752 bytes, below `0x380000`; SHA-256
  `d93de2ce1e788dd167c2a87dffd1908e2202c1bf399a8a6ba5c25f75e0224caa`.
- Signed bundle: 2,570,940 bytes; SHA-256
  `9810b31ece270c7b406be2eb605331ad460e83dc06134519e57498cb6270d069`.
- Local review bundle: `D:/Documents/.codex-reviews/Pajoniiir-JC4880-RC-v91-20261005/`.
  It contains copies of the original image/bundle/manifest/signature, their
  hashes and exact-source CI evidence. No image was rebuilt or re-versioned.
- Exact-source [CI run](https://github.com/dvucinozd/Pajoniiir/actions/runs/37331118116):
  all eight jobs successful, including both regular targets and host regressions.
- Changes after the firmware source commit are validation/status documentation
  only. Evidence commits do not change the frozen source/image identity.

Both original candidate and recovery bundle signatures, signed projects,
image sizes/hashes and separate manifest signatures were reverified locally.
Recovery is retained at `D:/Documents/.codex-reviews/Pajoniiir-recovery-M2.4-7-gb8d9cb7/`;
its image SHA-256 is
`0e9caf717808adbe2be526a10f59ddf768c1119eb2c914ccfe6b4fae15005d6c`.
Recovery availability and verification do not prove automatic boot rollback.

## Draft candidate changelog

- Preserve the previous product UI while integrating richer metadata,
  playlists/artwork, memory cues and persistent local hot-cue edits.
- Correct title/downbeat interpretation and improve seek, cue and loop handling.
- Retain compatible S3CP v2/v3 parsing and add v4 controller-profile capabilities.
- Correct the reproduced FLAC loop OUT/resize starvation and audio deadline
  regressions with bounded decoder prefix handoff and lower-priority health work.
- Retain signed board-specific OTA validation and bounded startup readiness.
- JC1060/DDJ-400/Pro DJ Link remain software configurations awaiting hardware
  qualification. Recorder and storage experiments remain outside ordinary builds.

## Verified hardware scope and remaining boundaries

The [hardware record](JC4880_L_CANDIDATE_20261005.md) contains actual operator
and telemetry evidence for real MP3/WAV/FLAC playback, MAIN/cue listening,
manual loop OUT/half/double/exit, scratch, CUE/PLAY/MT, local cue save/reload/
reboot/delete, D1 physical and Web load lock, stopped USB hotplug and soft reboot.
The final combined scratch/MT check passed 92.1710268 seconds with zero strict
fault deltas and operator-confirmed clean sound/release on both decks.
The operator-approved two-segment soak completed 10,801.3664176 credited
seconds, with a 9,785.4764933-second uninterrupted continuation and clean
operator listening. It is not an uninterrupted 180-minute run.

| Gate | Status |
| --- | --- |
| Exact-source CI / local firmware builds / signed artifact identity | PASS |
| Focused JC4880/FLX4 functional and listening scenarios above | PASS |
| Operator-approved segmented soak accounting | PASS |
| Uninterrupted 180-minute qualification | NOT RUN |
| Negative startup automatic rollback on hardware | NOT RUN |
| Source-cue tombstone reimport/cross-media physical isolation | NOT RUN |
| Remaining deck/format, shifted/touch load-lock, active removal and cancellation variants | NOT RUN |
| JC1060/DDJ-400/real Link peers | NOT RUN |
| Public release/tag/channel change | NOT AUTHORIZED / NOT PERFORMED |

The existing host startup gate covers timeout and readiness decisions; this
does not substitute for physically booting a failed pending image and
observing automatic rollback. The candidate is reviewable with these explicit
boundaries. Public production promotion still requires a decision on the
unrun release gates; this document does not silently waive them.
