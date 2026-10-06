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

The initial comparison is retained in `reviews/2026-10-06-ddj-ffl4-port-comparison.md`.
Its successful tests describe the two source trees, not the integrated image.
Integrated builds/tests and physical gates will be recorded separately here.
