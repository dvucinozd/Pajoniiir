# Documentation status

Status: **current documentation authority, reconciled 2026-10-06**.

## Shared-core migration authority

Canonical source is `dvucinozd/Pajoniiir`, branch `codex/shared-p4-core-m3`.
[Integration/provenance](SHARED_P4_CORE_INTEGRATION.md),
[board/capability policies](SHARED_P4_BOARDS.md),
[preserving wired migration](M3_SHARED_CORE_MIGRATION.md),
[explicit cue migration](M3_CUE_MIGRATION.md) and
[exact-image monitoring](RELIABILITY_MONITORING_PLAN.md) govern this candidate.
Earlier JC and M3 release/acceptance records retain their original image scope.
Build, package, installed-device, operator acceptance and publication evidence
must be recorded separately. Integrated physical gates remain NOT RUN.
The M3 repository remains historical and has not been remotely archived.

## Published product

The current public release is **M2.5**, frozen at
`20f1c3f04a615209ae25e9bbdae649d0f5b44e8d`. It is accepted for
**JC4880 / DDJ-FLX4 / PCM5102A MAIN / FLX4 headphone cue**, with the previous
product design and regular configuration. Preview, recorder and SD experiments
are disabled. The [release record](validation/M2_5_RELEASE_20261006.md)
is authoritative for artifacts, physical acceptance and publication.

| Item | Frozen release value |
| --- | --- |
| Release / annotated tag | `M2.5` |
| Source | `20f1c3f04a615209ae25e9bbdae649d0f5b44e8d` |
| Project / toolchain | `main-deck-p4` / ESP-IDF v6.0.2 |
| Application | 2,576,432 bytes; SHA-256 `7aaab31d4a7c00990939cb9ccecb04aba1cba463b1db919d168477c7df3210ba` |
| Signed bundle | 2,576,620 bytes; SHA-256 `44db191c1785168781e9b2893cac6dda1b92a20a5342c328d7fbfdbbd8707e27` |
| Public channel | [latest.json](https://ota.pajoniiir.eu/latest.json) |
| GitHub release | [M2.5](https://github.com/dvucinozd/Pajoniiir/releases/tag/M2.5) |
| Last publication-session device observation | M2.5 / `ota_1`, boot 618; decks stopped, AP/API and USB/FLX4 available, startup ready, zero allocation failures |

A recorded slot/boot is session evidence, not a fresh observation of the device.
Documentation and later diagnostic commits on `master` do not alter the frozen
image. The integration branch was merged into `master` on 2026-10-05; it is
no longer a separate source of unreleased A-L work.

## Acceptance and limits

Final M2.5 passed signed OTA, startup readiness, USB/FLX4 availability and
62.622211 seconds of dual playback with zero new strict audio/USB faults and
operator-confirmed Overview/MAIN/D1/D2 cue. The operator explicitly waived
a new 180-minute M2.5 soak because audio/deck/DSP code is unchanged. This is
an image-specific exception; the following v91 evidence remains historical.

Focused exact-v91 scenarios passed with operator-confirmed sound:
MP3/WAV/FLAC (including 96 kHz/24-bit FLAC), MAIN/D1/D2 cue, scratch,
CUE/PLAY/Master Tempo, manual loop OUT/half/double/exit, combined scratch/MT,
local cue save/reload/reboot/delete, normal D1 LOAD lock, stopped USB storage
and FLX4 reconnect, and post-reconnect playback.

The saved soak intervals total **10,801.3664176 seconds (180m01.37s)**:
1,015.8899243 seconds followed, after interruption, by 9,785.4764933 seconds.
The completed continuation has zero strict audio/USB fault deltas, valid
memory/stack reserves and operator-confirmed clean sound. This is an explicit
operator-accepted **segmented-soak release exception**.
**An uninterrupted 180-minute v91 soak remains NOT RUN.** Neither historical
monitor failure is erased or converted into PASS.

Diagnostic-only images separately passed unconfirmed-image rollback and
startup-readiness rejection at **60.253 seconds**, automatically restoring v91.
The latter forces the readiness input false while real AP/API remain available;
it does not prove an induced radio failure. Diagnostic images are excluded
from ordinary release packaging.

These scenarios remain NOT RUN on the frozen release:

- other deck/format combinations and full advanced transport variants;
- shifted/touch LOAD-lock variants;
- source-cue tombstone reimport and physical cross-media cue isolation;
- exhaustive playlist/artwork/PWV4, touch and metadata comparison;
- active removal/cancellation and unexercised held-control reconnect variants;
- JC1060, DDJ-400, other controllers, real CDJ/rekordbox Link peers, output
  phase/latency calibration and experimental SD/recorder qualification.

Unrun scenarios remain outside the accepted scope; software coverage does not
turn them into physical passes. Full details and failed intermediate candidates
are retained in the [hardware ledger](validation/JC4880_L_CANDIDATE_20261005.md)
and [release review](validation/JC4880_V91_RELEASE_REVIEW_20261005.md).
Automatic recovery passed in the accepted configuration. The earlier bare
M2.4 power-cycle observation is historical, not a universal v91 requirement.

A [focused library smoke](validation/JC4880_LIBRARY_SMOKE_20261006.md) on
installed v91 additionally confirmed artwork and memory-cue visibility,
navigation of `untitled playlist`, catalog consistency, local load and rejection
of stale catalog requests. Exported order, cue positions, downbeat/PWV4 source
comparison and artwork isolation remain open; the development logo was not
installed during these checks.

## Maintained architecture and software scope

Released M2.5 additionally includes the user-supplied default Pajoniiir
artwork logo. Overview and track Library rows use pre-rendered read-only
thumbnails when real artwork is unavailable; empty product decks and folder
rows remain distinct. It is absent from the immutable v91 image. Before final
M2.5, a clean `M2.4-111-ge09e9ca4` candidate from `e09e9ca4` was
installed in `ota_0` through signed OTA. Startup, API, FLX4 and storage
are available, with zero allocation failures, and the operator confirms logo
visibility in Library and on loaded D1 (`Red For Love {320}`). A matched
stopped-track memory comparison has unchanged largest internal/DMA blocks
and only eight bytes less free memory, matching BSS growth. A subsequent
62.419-second dual-deck audio check passed with operator-confirmed MAIN/D1/D2
cue sound, zero strict fault deltas and zero allocation failures. Largest
internal/DMA blocks remain 31,744 bytes during playback. The empty-deck
startup largest-block variation is retained in the record; this focused check
does not constitute a new exact-image 180-minute soak. The operator also
confirms correct real-cover-to-logo replacement and two-page Library browsing
without stale artwork. Media replacement and delayed/corrupt cover variants
remain open. See the
[candidate record](validation/M2_5_DEFAULT_ARTWORK_20261006.md).

One authoritative P4 core serves two board entrypoints:

| Configuration | Identity | Status |
| --- | --- | --- |
| JC4880 / FLX4 / PCM5102A MAIN + USB cue | `firmware/main-deck-p4` / `main-deck-p4` | Published frozen M2.5; focused physical scope and explicit soak waiver above |
| JC1060 / DDJ-400 / USB MAIN + cue / Ethernet Link | `firmware/main-deck-jc1060` / `main-deck-jc1060` | Software verified; all new-board/peer physical gates NOT RUN |

Packages A-L are software verified. The [integration ledger](FORK_IMPROVEMENTS.md)
maps modules and dated evidence, including adaptations from collaborator
[kayrozen](https://github.com/kayrozen), donor
`428b97dd4a175f03d3a172c8db9c4d5ed94195fb`. Persistent media identity,
audio-session fencing, worker ownership and signed OTA remain common-core
contracts. S3CP v2/v3/v4 parsing, compilation, storage and upload validation are
integrated; public Web Profile Builder deployment is a separate project gate.

JC4880 keeps USB0 storage, USB1 FLX4 and Wi-Fi service; it has no Link provider.
JC1060 Link is default OFF and Ethernet-only, with bounded discovery/claim,
browse, verified SD downloads and network sync. Unidentified NFS volumes use
session-local identity and fresh downloads, so local edits cannot migrate
between those sessions. LOCKED is a model state; sink/rate latency is
UNMEASURED until physically calibrated and engineering calibration is RAM-only.
No local browsable Link server is advertised.

The P4 owns playback, deck position, DSP, controller binding, LEDs and UI.
There is no active S3 firmware, UART control link or inter-board PCM bridge.
Compatibility names `control_link`, `S3CP` and `profile.s3bin` do not imply S3.
APTA remains on its separate branch and is outside this release.

## Security and inherited evidence

Shared service credentials, unchanged enclosed wiring and uncaptured numeric
thermal/RF/strain margins are accepted limitations. Secure Boot, Flash
Encryption and security eFuses remain disabled. Offline encrypted primary
and backup signing-key copies were operator-confirmed; backup recovery signing
and successor-key overlap remain future maintenance. See
[security policy](SECURITY_PROVISIONING_POLICY.md) and [risk register](RISK_REGISTER.md).

Earlier releases and validation records preserve only their own image results.
Start from the [documentation index](README.md) for the M2.1-M2.4 history,
duplicate-track-ID candidate acceptance, power/VBUS and lifecycle records.
Do not transfer historical passes to new code or claim waived tests were run.

## Source-of-truth order

1. Exact release artifacts/configuration and current code/tests for their own SHA.
2. [Published M2.5 record](validation/M2_5_RELEASE_20261006.md) for accepted
   hardware and publication; this status for current scope.
3. Operational architecture, wiring, OTA, security, checklist and risk documents.
4. Dated validation and integration history for their named images.
5. Git history for retired implementations and superseded plans.

The [documentation audit](validation/DOCUMENTATION_RECONCILIATION_20261006.md)
records this reconciliation. A documentation-only update does not reflash,
requalify or republish firmware.
