# Fork improvements package B software closure

> Historical / scenario-specific record, indexed 2026-10-06. Results, hashes
> and pending items below apply to the named images and sessions. They are not
> a current installed-device or public-channel claim. See the
> [v91 release](JC4880_V91_RELEASE_20261005.md) and [current status](../DOCUMENTATION_STATUS.md) for later acceptance;
> no NOT RUN or waived scenario is converted into PASS by this reconciliation.

Date: 2026-10-04. Status: **SOFTWARE VERIFIED on JC4880/P4**.
Hardware accepted: **NO**. Released/installed: **NO**.

Branch: `codex/fork-improvements`, M2.5 base `05296b8`.
Donor: kayrozen/Pajoniiir `428b97dd` (MIT); imported parser/seek helpers retain
attribution. B18 profile producer commit: `ffa343c9f2604a7ae8dc15df5e545b45ec193311`.
The B19 source commit contains this closure and its accompanying index changes.
Production M2.4, its OTA channel and the separate APTA branch are unchanged.

## Implemented behavior

| Area | Result |
| --- | --- |
| Seek and cue preroll | Source geometry is validated; decode/skip precedes publishing, paused history stays inaudible and session changes retire old work. Complete MP3 indexes support CBR/VBR with or without Xing/PVBR. |
| Duration and EOF | PDB, bounded PWV3 analysis span and live file length stay distinct. Complete frame index survives nonzero seek; sequential decoder EOF measures actual output without estimating from a seek coordinate. Audio drains before transport finish. |
| Loop resize | Timeline-aware trim/reseek removes only incompatible future PCM; already consumed history retains its ownership. |
| Cue persistence | Memory points/loops are separate from eight hot-cue slots, bounded to 16 with truncation status. Source merge, local edits/deletion and explicit restore use full persistent identity. V2 records remain authoritative and retained. |
| Transport | Touch/MIDI share CUE and pad semantics. Explicit VINYL/CDJ core actions and profile producers are available for both decks. |
| Load admission | Default-on load lock applies to playing destination decks and accepted asynchronous loads retain generation checks. |
| UI time | Session-bound live file duration controls elapsed/remaining/search bounds. Analysis waveform spacing remains fixed. Presentation redesign and memory-cue screens belong to package H. |

## B19 bounds and safe degradation

Index memory is fixed at 256 checkpoints per deck, with sparse checkpoint
compaction and exact frame numbers. No persistent index/cache is introduced.
It accepts complete constant-sample-rate MPEG Layer III streams up to 1 GiB
and 1,000,000 frames. ID3v2 prefixes, ID3v1 tails and at most 4 KiB zero padding
are handled; free-format MPEG, mixed-rate/corrupt streams and other trailing
tag types retain existing fallback and are not claimed sample-exact.

Firmware index build has a 30-second IO budget; seek header lookup has 2 seconds.
Both use the existing cache/media admission, cancellation and bounded yields,
outside AE_LOCK. An incomplete build cannot become an index hit. A complete
index failing subsequent seek IO reports `SEEK INDEX ERR`. These bounds need
physical latency qualification; they are not measurements of the board.

## Software verification

- Full `tests/run_p4_host_tests.ps1`: PASS, including executable suites and
  existing static contracts. New decoder seek checks cover CBR/VBR forward,
  backward, start and EOF-tail positions with exact remaining source samples.
- Focused audio engine: 481 PASS / 0 FAIL; track-length/index cases PASS.
- Controller format/compiler tests: PASS; v2/v3 behavior and FLX4 parity
  remain unchanged (12,288 input messages, 17 snapshot events, 690 LED cases).
- UI simulator: PASS, all eleven screenshot hashes unchanged.
- Deterministic dual-deck Master Tempo host soak: PASS, 300 virtual seconds,
  zero frame drift, synthetic click events and clipping.
- ESP-IDF 6.0.2 `main-deck-p4` build, unchanged dependency lock and application
  budget: PASS. Build size/result is recorded in the integration log.
- Documentation integrity and Git whitespace/staged diff checks: PASS.

Synthetic silent MP3 fixtures exercise frame geometry, tags, EOF and skip
accounting. They do not prove real codec quality or audible performance.
The optional operator-supplied real-MP3 host decode has not run in this closure.
JC1060 build qualification belongs to package E and is not claimed here.

## Hardware acceptance still required

| Gate | Status |
| --- | --- |
| Real MP3/WAV/FLAC, MAIN/cue listening and channel isolation | NOT RUN |
| CUE, scratch, VINYL/CDJ touch/profile actions and loop resize A/B | NOT RUN |
| Cue import/edit/delete/reboot/restore and media identity isolation | NOT RUN |
| Load lock across touch/controller/Web Remote and rapid load/seek/unload | NOT RUN |
| Index load/seek latency with another active deck and malformed fallback | NOT RUN |
| Internal/DMA heap, PSRAM, stack headroom and strict audio/USB counters | NOT RUN |
| Removal/reconnect, soft reboot and signed OTA/recovery | NOT RUN |
| Final 180-minute dual-deck soak on the exact candidate image | NOT RUN |

Rollback is a source/firmware return, with no partition or OTA schema change.
B16 metadata cache v5 is rejected/rebuilt by older firmware; original cue v2
records are retained. Do not install newer development profiles on M2.4 and
claim new jog actions there. A release requires an exact committed rebuild,
signed artifacts and the applicable physical/operator gates above.

Package B has no remaining software implementation item in its agreed scope.
S3CP v4/DDJ-400 is D; new board/audio/storage/UI/Link work remains E through L.
