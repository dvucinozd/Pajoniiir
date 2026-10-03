# Fork improvements software handoff — 2026-10-04

## Repository state

- Integration branch: `codex/fork-improvements`.
- Worktree: `C:\Users\Daniel\.codex\worktrees\fork-improvements\DDJ-FFL4`.
- Current pushed HEAD at this checkpoint: `83805190ed1d7cb7279481ffe564145389563efc`.
- Starting M2.5 source: `05296b8e8a2a10e38b3af58bb1e8034fbe806519`.
- Donor snapshot: kayrozen/Pajoniiir `428b97dd4a175f03d3a172c8db9c4d5ed94195fb`.
- Production M2.4 and the separate APTA branch have not been changed by this
  integration branch.

## Implemented and pushed

| Step | Commit | Software result |
| --- | --- | --- |
| A — PDB title index 17, PQTZ downbeat, host fixture/runner | `692b91ac` | Verified |
| B1 — malformed/out-of-file PVBR rejection | `e742199b` | Verified |
| B2 — paused CUE preroll without output stall | `b433f219` | Verified |
| B3 — ID3/PVBR origin and decoded-frame seek skip | `6b386106` | Verified |
| B4 — separate PVBR analysis span from file seek length | `0dfa055d` | Verified |
| B5 — queued PCM reconciliation on loop resize/exit | `83805190` | Verified |

Each step was pushed to `origin/codex/fork-improvements`, and local/remote HEAD
matched after B5. Detailed behavioral notes, donor provenance and validation
results are in [the integration log](../FORK_IMPROVEMENTS.md). At B5, the full
P4 host runner, ESP-IDF v6.0.2 P4 build, deterministic 300-second dual-deck
Master Tempo soak, documentation check and diff check passed. The application
was 2,510,528 bytes, 1,159,488 below the `0x380000` binary budget. The pinned
P4 dependency lock did not change. These are software checks on the source
working tree; they do not assert an installed image or physical acceptance.

## Open gates and next software work

- JC4880/FLX4 title/downbeat, tail seek, paused CUE, scratch and loop A/B,
  MAIN/cue listening, USB lifecycle and exact-image dual-deck soak: **NOT RUN**.
- JC1060, DDJ-400 and real CDJ/Rekordbox Link peers are unavailable:
  bring-up, audio routing, phase measurement, network interoperability and
  recovery/OTA acceptance: **NOT RUN**.
- No firmware from this branch was flashed, signed or released; M2.4 remains
  the production OTA baseline.
- Continue package B with load lock, memory/local cue merge, transport rules,
  and remaining duration/EOF cases. Then implement packages C–L in reviewable
  steps. Keep APTA outside this program and recording experimental.

Continue host, simulator and reproducible-build work without hardware. Record
physical cases as `NOT RUN`, never as PASS inferred from host results. Push each
finished software step and verify the remote SHA.
