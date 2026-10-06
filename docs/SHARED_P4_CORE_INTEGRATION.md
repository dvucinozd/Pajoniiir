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

The initial comparison is retained in `reviews/2026-10-06-ddj-ffl4-port-comparison.md`.
Its successful tests describe the two source trees, not the integrated image.
Integrated builds/tests and physical gates will be recorded separately here.
