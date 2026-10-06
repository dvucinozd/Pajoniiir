# Shared P4 core integration

Status: implementation in progress; no new hardware acceptance or publication.

Canonical repository: `dvucinozd/Pajoniiir`. Work branch: `codex/shared-p4-core-m3`.
Frozen donor base: `9af99cd234e775521f3ec83c0104f5c6a0c72920`.
Frozen M3 source: `e95417c4e2fea007d2c1dcb693790914692c62ba`.
The M3 review branch, not its older master, is the migration source.

## Adoption matrix

| Area | Authority and integration | Gate |
| --- | --- | --- |
| Shared layout/startup | Donor core, neutral component location, board providers | Three clean IDF 6.0.2 builds, host regressions |
| Display/touch/scanout | Each accepted BSP; M3 waveform-first/top-to-bottom policy | Board simulator and physical panel/touch |
| DSP/output | Donor transport plus M3 consumer PCM timeline/cache/FIR and fixed 48 kHz | PCM onset/timeline and 300 s keylock soak; audible gate |
| P01 OTA | Distinct `main-deck-m3`, retain other project identities | Cross-board rejection before flash, package verification |
| P02 persistent cues | Full donor media identity; preserve unbound M3 legacy records | Collision, migration and interrupted writes |
| P03 LOAD LOCK | Donor shared semantic admission, default ON | Input parity and asynchronous PLAY/load race |
| P04/P05/P06 Rekordbox | Donor tagged PCPT, PQTZ 1-4, title index 17 | Production-parser binary fixtures |
| P07 transport | Donor seek/preroll/live length/loop/CDJ behavior | Format, EOF, onset, loop and mixed-rate gates |
| P08 startup | Actual requested AP/HTTP readiness, bounded pending gate | Timeout/rollback, optional peripherals absent |
| Library/UI | Donor playlists/artwork/PWV4/memory cues, board layout policy | Stale generations, bounded cache, reviewed screenshots |
| USB/controller | Shared profiles v2/v3/v4/recovery; retain M3 ownership and epoch fixes | Lifecycle harness, profile corruption/reconnect, physical root mapping |
| Diagnostics | Shared bounded persistent journals/resources plus M3 monitor | Crash/restart tests and exact-image monitoring |
| Experiments | Shared recorder/Link/network services, M3 defaults OFF | Software variants; separate physical qualification |
| P09/P10 CI/docs | All targets and codex branches; canonical status/provenance | Clean locks, binary budget and documented NOT RUN gates |

## Invariants

One production implementation per portable subsystem. Exactly one BSP links per
target. Physical pin/USB topology, C6 lifecycle and scanout differences belong
in board providers. No S3, UART bridge or Ethernet is reintroduced on M3.
Preserve module-specific licenses and existing immutable release artifacts.

The wired M3 migration preserves the 16 MiB partition layout and NVS. Factory
write/readback precedes OTA-selection reset. Legacy track-ID cues are never
silently bound to an unrelated media identity. Public channel changes require
separate authorization after exact-image physical acceptance.

## Validation status

Phase 1 software gate PASS: full P4 host runner exit 0; IDF 6.0.2 builds and
`check_board_build.py` pass for JC4880 (2,576,928 B) and JC1060 (2,633,968 B).
The initial builds found a relocated BSP-private timer dependency, now fixed.
Only dependency-lock manifest hashes changed; resolved versions/hashes did not.
Real external MP3/PDB optional cases remain SKIP in this phase's host run.
No flash, physical acceptance, remote push or publication has occurred.

Phase 2 software gate PASS: all three IDF 6.0.2 targets build and pass BSP,
dependency and descriptor checks: JC4880 2,600,720 B, JC1060 2,657,632 B,
M3 2,562,000 B. The full host runner passes, including M3 scanout primitives,
board capability selection, scheduler ordering, Wi-Fi decisions and FIR/cache
regressions. The regular and M3-policy 300 s MT soaks pass with zero source-clock
drift and no detected clicks/clipping. Compact and wide product simulator
screenshots match the existing baselines without updating them. Linked USB/SD
wrappers and float32-only keylock/resampler objects were verified on all boards.

The shared DSP retains donor transport/timeline publication and uses board
policy for the M3 FIR reuse and denser bounded correlation search. JC defaults
retain the original faster search and unfiltered resampler. M3 uses fixed 48 kHz
MAIN, RGB888 native scanout and waveform-first/top-to-bottom updates. Wi-Fi
retains JC WPA2/WPA3/STA and M3 WPA2/APSTA policies, coherent M3 readiness, and
the shared exclusive OTA/probe/control lease and bounded association retries.
Real external MP3/PDB cases remain optional SKIP until the qualification fixture
gate is added. M3 image memory headroom and all physical gates remain NOT RUN.

Phase 3 software gate PASS: full host runner exit 0, three IDF 6.0.2 builds,
all six wrong-board manifest combinations, signed artifact identity tests,
M3/JC channel separation and independent CMake version-history tests pass.
M3 uses `M3-dev-g<12-hex-SHA>` until its own release ancestry exists; dirty
sources are labelled and pull discovery is disabled for development versions.
Explicit release builds require a clean source and a bounded numeric version.
Packaging checks compiled source provenance and board isolation before signing.
The public-channel generator refuses development M3 versions. `/api/status`
now reports board/project/source identity, product capabilities and whether
pull updates are supported; the web check button follows that state.

NVS initialization failures now preserve storage and fail startup, including
NO_FREE_PAGES/NEW_VERSION cases, instead of erasing settings and legacy cues.
Pending OTA retains the production 60 s actual AP/HTTP readiness gate; absent
USB peripherals are not required. Startup readiness/timeout/rollback decision
tests pass. Physical interrupted upload, boot timeout and rollback remain
NOT RUN on the integrated image. No device was flashed and no channel changed.

Phase 4 software gate PASS: full host runner exit 0 and all three IDF 6.0.2
builds pass with unchanged dependency locks. Standard PQTZ phases 1–4 and
downbeat 1 are covered by a real binary section fixture, alongside existing
title-index-17, tagged PCPT, memory/Hot Cue list and persistent merge regressions.
The new read-only paged catalog API exports P4 FAT metadata and persistent IDs
without loading ANLZ or changing decks; generation/file-change tests execute
the production catalog helper.

The offline legacy-cue migration tool requires the selected old export and
backup SHA, runs the production PDB parser, validates official NVS CRC/chunk
structures, and compares every setting/key after generating the merged image.
Ten executable tests cover lossless settings/blob preservation, ambiguous IDs,
wrong exports, missing audio, conflicting newer cues, damaged backups and a
complete CLI round trip. Old blobs remain intact; absent slots do not create
tombstones. See `M3_CUE_MIGRATION.md`. Software preparation performs no device
write. Wired NVS apply and physical set/delete/restore/reboot remain NOT RUN.

Phase 5 software gate PASS: complete host runner with `-Qualification`, all
three IDF 6.0.2 targets, unchanged locks and compact/wide screenshot baselines
pass. M3-policy 300 s dual-deck MT soak passes with zero source-clock drift,
no detected clicks or clipping. Mandatory committed MP3/WAV/FLAC/PDB/ANLZ
fixtures now have checked hashes; qualification cannot silently skip them.
Lossless PCM is checked sample-for-sample, including short/final EOF batches,
and mixed-rate seek executes both production decoders.

Real VBR media reproduced position-dependent seek onset with the original
four-frame reservoir lead. Sixteen-frame preparation makes all three seek
positions retain the same full-decoded PCM/codec-delay mapping; the MP3 relative
onset bound is 96 frames, WAV/FLAC one frame. PVBR regressions retain byte/frame
identity and update their preparation-time bound accordingly. Full-track WAV
export rewinds and is no longer described as proof of acoustic seek.

LOAD replacement now reserves the semantic deck transport from final admission
through audio replacement and loaded-track publication. MIDI/touch/web actions
share the same actor boundary. If PLAY wins, LOAD is refused without mutation;
if LOAD wins, transport cannot enter between check and reset. Generation tokens
prevent stale completion from releasing a newer reservation. The real deck-core
tests cover both orders, other-deck independence and LOAD LOCK off. Physical
seek/loop/CUE, replacement races and opposite-pitch acceptance remain NOT RUN.

Phase 6 software gate PASS: full host qualification exits 0, all nine simulator
configurations pass, and all three IDF 6.0.2 builds pass with unchanged locks:
JC4880 2,604,448 B, JC1060 2,661,616 B, M3 2,565,616 B.
Display integration adds a product M3 simulator using the shared board
capabilities, waveform-first update and top-to-bottom scheduling. Artwork uses
a board layout policy: the M3 cover fits inside the title row, preserving both
648 x 141 main waveforms at x=82/y=0,142 and the accepted 392 x 45 full-track
waveforms. JC layout and screenshot baselines remain unchanged. M3 retains
Wi-Fi ON and PCM5102A MAIN/USB cue in its simulated Settings.

The M3 scripted scenario checks all five zoom geometries and includes reviewed
screenshots for playlists/folders/export order, memory cues, source/local Hot
Cues, missing/restored artwork, PWV4, screensaver and Settings restoration.
These are software rendering/navigation checks, not physical scanout timing,
touch or audio acceptance. Final hardware gates remain NOT RUN.

Phase 7 software gate PASS: full host qualification and all three IDF 6.0.2
builds exit 0 with unchanged dependency locks and Phase 6 image sizes.
It uses the existing shared USB/controller infrastructure on every board:
the sole MSC mount owner, deferred/coalesced semantic input, connection and
transport epochs, nonblocking MIDI OUT and producer fencing before teardown.
The MSC transfer object and sector count both retain the 8 KiB byte bound,
including sector sizes other than 512 B. Recovery is scoped by immutable root
roles; unknown device routing defers admission and cannot trigger a global
power cycle. M3 retains 256 MIDI OUT queue entries, including shifted pad banks.
Profiles v2/v3/v4 use the same compiler/parser/storage/runtime, atomic activation
and held-state reconciliation; built-in fallback is exact FLX4 VID/PID only.

The frozen M3 mapper/LED code is test-only and its original source hashes are
verified after reversing namespacing. Direct parity executes 1,834,758 messages
and 131,072 LED combinations. Two documented donor FX-target differences are
retained: wrong-channel selector notes are rejected and redundant identical
target publications are permitted. Valid selectors retain the same semantic
state, including both/release fallback; all other input and LED bytes agree.
USB CIN padding beyond message length is ignored by both semantic mappers.
M3 storage throughput, actual root enumeration/reconnect, FAT32/exFAT playback
and profile/UAC hardware acceptance remain NOT RUN.

Phase 8 software gate PASS: complete host qualification exits 0, all nine
simulator configurations retain their baselines, and all three regular IDF
6.0.2 builds pass with unchanged dependency locks: JC4880 2,583,840 B,
JC1060 2,642,288 B, M3 2,565,136 B. M3 recorder (2,578,416 B), alternative UI
(2,580,400 B) and STA Link (2,627,504 B) build independently with fresh config.
CI now requires these M3 variants alongside existing JC variants and the shared
host gate. The board verifier checks every canonical core component's path,
single BSP, feature class, compiled source and application identity.

Pro DJ Link browse/download/cache/sync now lives in the shared core and receives
an explicit network provider. JC1060 retains Ethernet; M3's optional experiment
uses STA only, with no AP/default-route fallback. Normal M3 does not start Link.
Recorder/preview remain separate experiments. Network clock mutation now shares
the LOAD transport reservation and is tested against an active replacement.

The FIR/history feature stores no inactive arrays on JC. Linked two-deck states
use 6,576 B on JC versus 36,304 B on M3, reclaiming 29,728 B on each JC target.
JC and M3 five-minute production keylock soaks retain zero clock drift and no
detected clicks/clipping. USB/SD wrappers are linked; resampler objects contain
no software double helpers. Runtime heap/stack reserves still need hardware.

Existing bounded retained audio-WDT/library-load journals, rotated service
journal, allocation callback and self-sampled stack counters are shared.
The monitor performs GET only, binds board/project/source/ELF, detects resets and
strict counter loss, and preserves original output-late investigation rules.
Five executed tests cover clean/isolated-late, losses, bursts, missing/changed
identity, reboot and workload mismatch. `/api/firmware` exposes IDF running-image
state separately from OTA transfer state; a signed manifest binds ELF and full
binary/bundle digests. Physical crash/restart/resource and experiment gates
remain NOT RUN.

Migration software gate PASS: twelve executed tests cover full/region backup,
old/new signed project identity, MAC/security/layout, app checksum/appended SHA,
stale flash refusal, factory readback failure, interrupted selection write,
unchanged settings/other regions, source-bound cue-only follow-up and signed
pending-to-VALID observation. The real esptool adapter is pinned to 5.3.1 and
has no full-chip erase path. Offline plan is reviewable; every device mutation
stage is recorded before the write and failures do not request a reboot.
See `M3_SHARED_CORE_MIGRATION.md`. Serial/HTTP behavior is mocked in this gate;
actual device migration, operator settings and physical acceptance are NOT RUN.

README remains English. Active architecture/profile/parser paths, development,
startup, OTA, board policies, documentation status and risk register now point
to the canonical shared architecture. Earlier source/release validation records
retain their hashes/paths and do not qualify the integrated image. The final
clean build, signed package and exact-SHA CI are separate candidate evidence;
no public channel or existing release asset was changed.

| Finding | Implemented software outcome | Open integrated-image acceptance |
| --- | --- | --- |
| P01 | Separate signed project/chip/version, channel and embedded descriptor checks | Signed install/interruption/wrong-target physical check |
| P02 | Full media identity, source/local merge, tombstones/restore, explicit legacy migration | Cue migration/set/delete/restore/reboot on selected exports |
| P03 | Default-on shared LOAD LOCK and atomic worker/transport reservation | MIDI/touch/web replacement while both decks play |
| P04 | Standard tagged PCPT parsing and memory/Hot Cue list type | Real export comparison |
| P05 | PDB title index 17 | Real export metadata/UI review |
| P06 | PQTZ phase 1-4 and downbeat 1 | Real export grid/phase review |
| P07 | Decoder seek/preroll/length/loop/CDJ and required PCM/media regressions | MAIN/PFL seek/onset/EOF/loops/CUE/MT/mixed rates |
| P08 | Critical initialization plus requested real AP/DHCP/HTTP pending gate | Startup timeout/rollback on exact candidate |
| P09 | Three ordinary targets, experiment matrix, no-SKIP qualification, isolation/budget/locks | Exact-SHA hosted CI and board hardware qualification |
| P10 | Canonical docs/provenance/migration/capabilities and evidence separation | Final installed/operator campaign record |

No integrated target has a new production PASS until its hardware gates pass.
Candidate qualification requires the exact-source 12-job board/host matrix,
the separate USB software-harness workflow and documentation integrity. Missing,
failed, cancelled or skipped jobs reject candidate evidence. The USB harness
links the pure shared capability descriptions without pulling a physical BSP;
production adapters retain exactly one board provider. Historical documentation
links point to their frozen source revisions when a referenced artifact is not
part of the canonical repository.

M3 Campaign A/B, waveform timing/artwork/Wi-Fi load and physical audio/operator
confirmation remain NOT RUN. Isolated output-late stays monitoring, not zero-late.

The initial comparison is retained in `reviews/2026-10-06-ddj-ffl4-port-comparison.md`.
Its successful tests describe the two source trees, not the integrated image.
Integrated builds/tests and physical gates will be recorded separately here.

The initial M3 candidate from clean source 95e6573c has now been installed and
confirmed VALID in ota_0 after signed local OTA. GUI/touch/backlight and preserved
settings were confirmed before OTA. See the
[installation record](validation/M3_SHARED_CORE_INSTALL_20261006.md) for exact
digests, the operator-authorized existing-image recovery mode, manual RESET and
remaining physical gates. Migration tooling now has 19 passing regressions.

The preceding ordinary candidate is clean source 11574f4e, signed and installed
VALID in M3 ota_1. Its status exposes MT/loop state and its read-only monitor
supports the common catalog contract with seven passing regressions. All three
ordinary builds and exact-source CI passed. The replacement medium contains 324
tracks; 44.1/48-kHz loads and stopped loop/pitch preflight passed. See the
[current image record](validation/M3_SHARED_CORE_11574_20261006.md).
Active-playback/operator, Campaign A/B and startup-timeout/rollback gates remain
open. These updates supersede the earlier installed-state and test counts above;
they do not extend earlier physical acceptance to the new image.

The 11574f4e timing attempt failed on operator-confirmed watery dual waveforms.
Solo D1 was sharp; dual playback also failed without API polling. The current
clean candidate c698fa67 reserves the early frame for both waveform phases
before either deck's status/artwork/chrome, preserving the board policies and
one interpolation per deck. It passed host qualification, existing simulator
baselines, all three ordinary builds and exact-source CI, and is installed VALID
in M3 ota_0. Its operator retest failed: watery waveforms persisted and visual
stutter appeared while audio remained clean. Bounded RAM scanout timing counters
are the next diagnostic step. Physical acceptance remains failed in the
[regression/retest record](validation/M3_SHARED_WAVEFORM_REGRESSION_20261006.md).

Measurement candidate 2fa8a637 is now signed and installed VALID in M3 ota_1.
All three builds and exact-source CI passed. The operator again confirmed solo
D1 sharp, dual deformed and clean sound. A roughly 12-second dual window measured
57 coalesced refreshes and a 15.5-ms mean lower-waveform finish after refresh.
The next focused repair removes duplicate PPA cache sync and unchanged-frame
resampling; neither this measurement nor a successful build is visual acceptance.
