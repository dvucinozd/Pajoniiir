# P4 post-release development plan

Shared-core/M3 implementation is tracked in
[the integration ledger](SHARED_P4_CORE_INTEGRATION.md). Portable code now lives
under `firmware/p4-core`; hardware providers live under `firmware/boards`.
This development branch does not extend the physical scope of M2.5.

The shared-core implementation retains donor library/transport/profile features
and accepted M3 BSP/DSP/UI policies in one runtime. Finish software variant gates,
then use [M3 wired migration](M3_SHARED_CORE_MIGRATION.md) on the frozen candidate.
New-image physical gates include GUI/touch, dual-deck MAIN/PFL, all five zooms,
signed OTA/startup/rollback, 30 Campaign A cycles and at least 60 minutes of
worst-case Campaign B timing. Legacy-image acceptance does not transfer.
Unavailable hardware is NOT RUN. Public board-channel publication requires
separate authorization after that board's acceptance.

`M3-dev-g180b008c4f19` is installed in M3 ota_0 with VALID health state,
verified against its exact clean source and ELF on 2026-10-07. The install
script's earlier 100-second polling deadline expired; the later runtime
observation does not establish startup timing within that deadline.
[The current candidate record](validation/M3_SHARED_WAVEFORM_REGRESSION_20261006.md) separates
the signed OTA, 324-track catalog and mixed-rate load evidence from remaining
operator audio/UI and USB campaigns. Initial wired migration and earlier GUI/
touch/settings confirmations remain in the image-specific
[95e installation record](validation/M3_SHARED_CORE_INSTALL_20261006.md).
Both 11574f4e and the installed c698fa67 ordering repair failed on
operator-confirmed dual waveform deformation. The latter also showed visual
stutter while audio remained clean. The measurement candidate reproduced the
failure: 57 coalesced refreshes in about 12 seconds of dual playback. The next
repair, now installed after all three builds and exact-source CI passed,
removes duplicate PPA cache maintenance and unchanged frame resampling;
see [the regression record](validation/M3_SHARED_WAVEFORM_REGRESSION_20261006.md).
The installed repair also failed the physical retest: solo sharp, dual deformed,
audio clean, 55 coalesced refreshes in the short dual window. Investigate the
remaining blocking UI status reads before another candidate; the full timing
gate must restart after a successful exact-image physical retest.
The next candidate shares one zero-wait, UI-owned audio-status observation per
deck between position, session-checked duration and status/progress rendering.
Busy decode retains the last observation; transport decisions keep their
authoritative APIs. Production mutex-contention and simulator recovery tests
precede clean builds and physical acceptance.

Status: **M2.5 published; A-L merged into master; hardware extensions open**
(2026-10-06).

## Current baseline

The accepted release is `M2.5` from
`20f1c3f04a615209ae25e9bbdae649d0f5b44e8d`, for JC4880/FLX4 with PCM5102A
MAIN and FLX4 cue. The previous product design is retained. The
[release record](validation/M2_5_RELEASE_20261006.md) preserves the explicit
final-image soak waiver, exact artifacts and focused physical scope.

Packages A-L are software verified and merged into `master`. The
[integration ledger](FORK_IMPROVEMENTS.md) records modules adapted from
[kayrozen](https://github.com/kayrozen) and their test/provenance history.
JC1060/DDJ-400/Ethernet Link is a development configuration, not released
hardware support. The recorder, SD experiments and optional UI preview remain
off in regular builds. APTA is outside this integration.

Physical rollback and the 60-second startup rejection passed using isolated
diagnostic images. They restored the original v91; those later diagnostic
commits are not the hardware-qualified release image. Ordinary packaging
rejects both fault-injection flags. Original tags and versioned assets remain
immutable.

## Next work

M2.5 JC4880/FLX4 is published after exact-source CI, final tagged OTA/startup
and operator-confirmed short audio acceptance. Subsequent maintenance uses
change-driven regressions; the remaining local-library cases below stay open.
JC1060/DDJ-400/Link, experimental recording and APTA need not block a release
that explicitly excludes their production support. Web Profile Builder v4
deployment is required only if its public exporter is part of the release scope.

The user-supplied Pajoniiir logo is now the released M2.5 artwork fallback for
loaded tracks and Library rows. It is stored as native 34x34/40x40 RGB565
constants; the existing real-artwork worker/cache remains authoritative.
Candidate `M2.4-111-ge09e9ca4` has passed signed OTA, operator-confirmed logo
display and a 62.419-second dual-deck listening/resource check; see the
[candidate record](validation/M2_5_DEFAULT_ARTWORK_20261006.md).
Real cover, missing cover and normal page/media return passed on the logo
candidate. Final M2.5 passed 62.622211 seconds of clean dual playback and
startup/resource checks. Its new long soak was explicitly waived; v91 remains
immutable and its earlier evidence is not relabeled as M2.5.

| Work | Required evidence / boundary |
| --- | --- |
| JC4880 maintenance | Change-driven regressions on a new exact image; do not rebuild current master and call it the accepted v91 |
| Remaining local-library/transport variants | Real title/downbeat, ordered nested playlists, artwork/PWV4, source-cue restore/reimport and cross-media identity; advanced touch/shift and active-removal cases remain NOT RUN |
| JC1060 bring-up | Board/panel revision, flash/PSRAM, touch/render, USB root roles, SD, RMII PHY/DHCP, memory/stack/deadline measurements |
| DDJ-400 and other controllers | Real descriptors, MIDI/LED/SysEx/reconnect and MAIN/cue channel isolation; profile fixtures alone do not qualify hardware |
| Link I/J | Real CDJ and separately named rekordbox version; discovery claims/conflicts, ordered browse, NFS downloads, cancel/media-swap/cache/space/power-loss cases |
| Link K | Measured output phase, drift, master handoff and listening per sink/sample-rate; LOCKED alone is insufficient |
| SD/recorder experiments G | Exact-card A/B latency, ring overflow, finalize/removal/full-card/power-loss, simultaneous audio deadlines and listening; separate experimental result |
| Web Profile Builder | Coordinated v4 exporter already has software evidence; deployment/acceptance belongs to that repository and is not implied by firmware publication |
| Security maintenance | Per-device credentials if exposure expands, disposable backup signature check, successor-key overlap and sacrificial hardware-rooted protection pilot |
| LIBAPTA | Separate branch/program; revalidate upstream and entry gates in [its plan](LIBAPTA_P4_INTEGRATION_PLAN.md) |

JC1060 must keep PCM5102A disabled because its pins overlap Ethernet;
ES8311 is disabled in the USB-only configuration. No Wi-Fi Link or local
NFS/DBServer library server is part of this program.

## Maintenance rule

Repeat gates because behavior or assumptions changed, not because records aged.

| Change area | Minimum checks |
| --- | --- |
| Documentation/media | Documentation integrity, whitespace, links; visual Pages review for manual layout changes |
| Tool/profile format | Relevant suites; parser/compiler/storage/upload compatibility and generated fixtures together |
| Shared firmware | P4 host runner, affected IDF v6.0.2 target builds, lock stability and image budget |
| UI | Actual presentation simulator layouts, visual review, exact-image display/touch and active playback checks |
| USB/storage/audio/OTA | Relevant focused hardware regression, counters, operator sound and lifecycle checks |
| DSP/scheduling/buffering | Reproduce/fix the failure; exact-image active timing and appropriately long audible test |
| Partition/bootloader/trust key | Wired recovery plan, isolated signed build and supervised installation |

Shared-core changes require all three entrypoint builds. Use committed independent
dependency locks and the fixed `0x380000` image budget. Preserve actual source,
configuration, version, slot/boot, strict deltas and operator results.
A host soak or simulator cannot replace physical deadlines or sound.

## Release discipline

- Freeze an exact clean source and configuration; run applicable automated gates.
- Install and accept that exact image for a named board/controller/sink/peer set.
- Default duration gate remains an uninterrupted 180-minute dual-deck run.
  The recorded v91 segmented exception is image-specific, not an automatic
  waiver for later releases.
- Publish immutable signed artifacts before atomically switching the correct
  board channel; independently verify public size/hash/signature and assets.
- Record NOT RUN and explicit exceptions separately from PASS.
- Never move published tags or overwrite frozen assets.
- Do not use fault-injection, preview, recorder or storage-experiment images as
  ordinary production releases.
- Keep private keys and hosting credentials outside Git/logs/artifacts.
- No eFuse operation is implicit in release tooling.

## Historical plans

The [M2.4 record](validation/M2_4_PRODUCTION_RELEASE_20260929.md),
[M2.5 handoff](M2_5_HANDOFF_20261002.md),
[H migration history](validation/FORK_IMPROVEMENTS_PACKAGE_H_PROGRESS_20261004.md)
and [L hardware ledger](validation/JC4880_L_CANDIDATE_20261005.md) retain
older installation, supply, resource and audio-failure sequences. Their former
"next step", installed-version and unpublished-channel statements describe
those sessions; current status is [DOCUMENTATION_STATUS.md](DOCUMENTATION_STATUS.md).
Do not reopen completed v91 gates without a new failure or changed assumption.
