# Fork improvements integration

Work in progress on `codex/fork-improvements`, starting from
`05296b8e8a2a10e38b3af58bb1e8034fbe806519`. Production remains M2.4.
Donor: [kayrozen/Pajoniiir](https://github.com/kayrozen/Pajoniiir/tree/428b97dd4a175f03d3a172c8db9c4d5ed94195fb),
frozen at `428b97dd4a175f03d3a172c8db9c4d5ed94195fb` (v323).

## Scope and delivery status

One P4 core must serve JC4880/FLX4 and JC1060/DDJ-400. Ethernet Link belongs
only to JC1060; recording remains an opt-in experiment. APTA integration is
outside this branch. These are planned capabilities, not current support claims.

| Package | Scope | Current state |
| --- | --- | --- |
| A | Baseline host tests, independent functional suites, PDB title, PQTZ downbeat | Software verified; physical acceptance NOT RUN |
| B | Accurate seek, duration, loop resize, memory/local cues, load lock | B1 PVBR bounds verified; remainder pending |
| C | Hierarchical playlists, bounded artwork, PWV4 | Pending |
| D | S3CP v4, all validators/compiler/exporter, DDJ-400 profile | Pending |
| E | Board adapter, JC1060 entrypoint/BSP, dependency lock and CI | Pending |
| F | Qualified UAC formats, MAIN routing, consumer-paced USB audio | Pending |
| G | IDF 6.0.2 SD idle workaround, measurements, experimental recorder | Pending |
| H | Shared dj_ui presentation at 800x480 and 1024x600 | Pending |
| I | MIT djlink codec, Ethernet discovery/claim/browse | Pending |
| J | Full persistent media identity, cancellable download, atomic cache | Pending |
| K | Epoch-bound network clock, controlled sync and tempo master | Pending |
| L | Project-bound OTA, documentation, qualification and release candidate | Pending |

Software verification, hardware acceptance and release are separate states.
JC1060, DDJ-400 and real CDJ peer acceptance remain **NOT RUN** until the
hardware is available. No deployment, production channel change or hardware
acceptance follows from a successful host test or build.

## Package A

The PDB parser reads title from DeviceSQL string index 17, retaining filename
fallback. New synthetic fixtures put distinct strings at indices 17, 18 and 19
and cover empty/UTF-16 titles and an out-of-range offset.

PQTZ numbers are 1..4, with 1 denoting a downbeat. Shared helpers now drive
overview grid, renderer and beat indicator. Unknown/invalid numbers cannot
become highlighted downbeats. The indicator's internal UI phase remains 0..3;
timestamps and cue positions are unchanged. Simulator fixtures retain the same
musical accents using correctly numbered PQTZ data.

The host runner accepts `-Suite` and `-ListSuites`. Focused runs explicitly
report that they are not the complete regression gate. Default CI still runs
all checks, with the early source-text contracts deferred until executable
suites finish. The previously standalone beat-indicator suite is now registered.

```powershell
$env:Path = "$env:Path;C:\msys64\ucrt64\bin"
.\tests\run_p4_host_tests.ps1 -ListSuites
.\tests\run_p4_host_tests.ps1 -Suite rekordbox_pdb,ui_beat_indicator,ui_overview_grid,ui_overview_renderer
.\tests\run_p4_host_tests.ps1
.\tests\ui_simulator\run_ui_simulator_e2e.ps1 -KeepArtifacts
```

Initial M2.5 full host regression passed before edits. Runtime internal/DMA
heap, PSRAM usage, stack headroom and strict audio/USB counter baselines have
**not been measured** on this branch. Record those against the exact installed
image before accepting hardware changes. Build size alone is insufficient.

Verification on 2026-10-03:

- Focused PDB, beat indicator, overview grid and renderer suites: PASS.
- Complete P4 host runner, including deferred source contracts: PASS (exit 0).
- ESP-IDF 6.0.2 P4 build: PASS; `dependencies.lock` unchanged.
- Application: 2,505,440 bytes; 1,164,576 bytes below the 0x380000 budget.
- Build SHA-256: `685d59fa700d93cc22294bdbcf5d4155e82f0abaa38a84231cfa998000a44649`.
- UI simulator navigation and all seven screenshot comparisons: PASS;
  no baseline image was changed.

This is an uncommitted-source development build, not a signed release artifact.
The source commit will identify the reviewable package; a future release must
rebuild and qualify its exact frozen version.

## B1: PVBR validation before seek

The existing engine accepted a table when any entry after the first was
nonzero. It now requires a nondecreasing table with actual forward progress.
Once the worker has opened the source and obtained its length, every offset
must be strictly inside that file. A rejected table uses the existing seek
fallback; it does not prevent playback. Checks run in the load path, not once
per audio block, and the current PCM timeline remains intact.

This is an original integration safeguard required by package B, rather than
a copied donor module. It does not yet correct ID3-relative offsets, validate
MPEG frame alignment, implement exact seek skipping or resolve analysis versus
decoded duration. Those remain open package B work.

Verification on 2026-10-03:

- New `audio_pvbr_validation` suite: PASS (null/empty/constant/descending,
  repeated valid offsets, partial zero-filled tail and exact/outside EOF).
- Complete P4 host runner: PASS (exit 0).
- ESP-IDF 6.0.2 P4 build: PASS; dependency lock unchanged.
- Application: 2,505,632 bytes; 1,164,384 bytes below the 0x380000 budget.
- Build SHA-256: `ee387e854c3b4fda0f9ff8942e0b32b1cba780b9f934d5114b66f91aa1217e2c`.
- Deterministic dual-deck Master Tempo soak: PASS, 300 virtual seconds,
  zero sample drift and clipping. This is host evidence, not measured hardware
  deadline or audible acceptance.
- Physical MP3 cue/seek A/B, scratch and dual-deck output: **NOT RUN**.

The recorded build includes uncommitted B1 source. No persistent format changes
are involved; reverting B1 restores the previous table admission policy.

## Provenance and rollback

Package A reimplements the donor's title/downbeat correction in the current
upstream components; it does not replace components with older fork copies.
Donor network modules must retain their MIT license when imported. The future
SD wrapper must retain Apache-2.0 attribution and an explicit IDF version guard.
No such module is claimed imported until its package is implemented.

Package A changes no persistent storage, profiles, partition layout or OTA
manifest. Returning to the starting source restores prior behavior without a
data migration. Physical JC4880 acceptance still requires a real Rekordbox
export, title/downbeat inspection and touch/render smoke during dual playback.

## Required remaining integration constraints

- Never replace the current PCM timeline with the fork's older engine.
- Keep slow storage/network/JPEG work out of audio and LVGL draw callbacks.
- Bind asynchronous results to track identity, source generation and request epoch.
- Keep full 32-byte media identity for cue edits and remote cache manifests.
- V4 assigns NOTE_SELECT to raw type 9; raw type 8 remains CC7_TO14.
- Keep ESP-IDF 6.0.2, pinned USB fixes and the 0x380000 application budget.
- Reject unsupported USB feedback requirements; qualify FLX4 parity separately.
- Never use periodic hard seeks for ongoing network sync.
- Preserve `main-deck-p4` OTA identity; use `main-deck-jc1060` for the new board.
- Final qualification includes exact-image physical and listening gates, with
  unrun configurations explicitly listed rather than inferred from simulator CI.
