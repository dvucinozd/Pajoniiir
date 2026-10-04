# Fork improvements integration

Work in progress on `codex/fork-improvements`, starting from
`05296b8e8a2a10e38b3af58bb1e8034fbe806519`. Production remains M2.4.
Donor: [kayrozen/Pajoniiir](https://github.com/kayrozen/Pajoniiir/tree/428b97dd4a175f03d3a172c8db9c4d5ed94195fb),
frozen at `428b97dd4a175f03d3a172c8db9c4d5ed94195fb` (v323).
The [2026-10-04 software handoff](validation/FORK_IMPROVEMENTS_HANDOFF_20261004.md)
records the pushed checkpoint and hardware gates left unrun.

## Scope and delivery status

One P4 core must serve JC4880/FLX4 and JC1060/DDJ-400. Ethernet Link belongs
only to JC1060; recording remains an opt-in experiment. APTA integration is
outside this branch. These are planned capabilities, not current support claims.

| Package | Scope | Current state |
| --- | --- | --- |
| A | Baseline host tests, independent functional suites, PDB title, PQTZ downbeat | Software verified; physical acceptance NOT RUN |
| B | Accurate seek, duration, loop resize, memory/local cues, load lock | B1-B12 software verified; jog mode and remaining duration work pending |
| C | Hierarchical playlists, bounded artwork, PWV4 | C1-C5 software verified on JC4880 build and 800x480 simulator; physical acceptance NOT RUN |
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

## B2: cue preroll cannot stall before the cue

The paused CUE path previously reserved the entire two-second forward PCM cap
for history. The decoder needs space for one full MP3 decode batch before it
starts another iteration, so it could stop just short of the cue. The preroll
helper now leaves one maximum decode batch free, moves the timeline playhead
onto the cue as soon as that frame is written, and keeps both normal and
Master Tempo output gated while preroll is pending. A timeline generation
change invalidates late publication. At source EOF before the cue, the helper
publishes the actual last decoded point to release the gate.

The helper is adapted from donor `428b97dd` under the repository MIT license.
The upstream canonical PCM timeline, output position and transport ownership
remain authoritative. No persistence, controller ABI or OTA format changes.

Verification on 2026-10-04:

- New retained-PCM simulation: PASS at 22.05, 44.1, 48, 96 and 192 kHz,
  including short preroll, immediate PLAY gating, short EOF and reproduction
  of the old full-cap stall.
- Complete P4 host runner and extracted firmware lifecycle regression: PASS.
- ESP-IDF 6.0.2 P4 build: PASS; 2,505,952 bytes, 1,164,064 below the
  application budget; dependency lock unchanged.
- Deterministic 300-second dual-deck Master Tempo soak: PASS, zero frame
  drift and clipping.
- Physical paused CUE/scratch, MAIN/cue listening and dual-deck soak: **NOT RUN**.

Source commit and physical qualification must be recorded before calling this
hardware accepted. The next package B steps still include exact VBR seek,
duration authority, loop resize and persistent cue merge.

## B3: MP3 PVBR origin and decoded-frame seek

The MP3 decoder now establishes the ID3v2 payload boundary through the bounded
cache before using an imported PVBR table. Two table entries must point to
actual MPEG frames, and every offset must fit after the selected origin. The
seek locator uses the Xing/VBRI frame count where present, lands before the
requested frame to refill the MP3 bit reservoir, and discards decoded PCM up
to the requested position before publishing audio. This exact skip applies to
user seeks and paused CUE preroll with verified frame geometry. Loop wraps
retain their existing runway policy pending the loop resize work. Without a
trusted frame count, PVBR remains a bounded approximate seek. The no-table
estimate starts after a valid ID3v2 tag.

`audio_track_length` and `audio_seek_skip` are imported from donor `428b97dd`
under MIT. The broader duration resolution, EOF extension and remote-table
generation functions in that pure module are not yet wired into playback.
The original PCM timeline and P4 source/worker ownership remain in place.

Verification on 2026-10-04:

- Imported seek/track-length host suites: PASS, including synthetic ID3,
  Xing, VBR geometry, byte-origin, frame-skip and truncated inputs.
- Complete P4 host runner: PASS; ESP-IDF 6.0.2 P4 build: PASS.
- Application: 2,508,848 bytes, 1,161,168 bytes below the 0x380000 budget;
  dependency lock unchanged.
- Deterministic 300-second dual-deck Master Tempo soak: PASS, zero frame
  drift and clipping.
- MP3 cue/loop A/B and operator listening on the installed board: **NOT RUN**.

Rollback is source-only; no persistent data or OTA format changes. Accurate
end-of-track duration, WAV/FLAC behavior, loop resize and local cue merge remain
open in package B. This package's host evidence does not imply those gates.

## B4: Separate analysis span from seekable file length

The engine now retains the incoming Rekordbox duration as the PVBR analysis
time base. It no longer rescales the 400 table entries when a verified MP3
Xing/VBRI frame count indicates that the audio extends past the analysis.
The MP3 count is used only after two PVBR offsets resolve to valid MPEG frames,
fits within the physical file, and yields a duration within twice the analysis
span. The existing track-length tolerance avoids extending for rounding noise.
An absent or rejected count leaves the existing analysis-length seek estimate.
WAV derives its seek length from its validated data chunk; FLAC uses a nonzero
decoder frame count. Neither requires a PDB duration to be missing.

This changes seek bounds and tail interpolation, not the audible EOF rule or
the authoritative PCM timeline. Full no-header MP3 tail measurement, precise
PWV3 span refinement, measured EOF correction and UI duration publication are
still pending. No persistence, controller ABI, OTA schema or partition changes.

Verification on 2026-10-04:

- Track-length host cases cover a file 5% longer than its analysis: seek
  positions inside the analysis remain fixed and a tail seek lands past the
  final table entry. Complete P4 host runner: PASS.
- ESP-IDF 6.0.2 P4 build: PASS; application 2,509,296 bytes, 1,160,720
  bytes below the 0x380000 budget; dependency lock unchanged.
- Deterministic 300-second dual-deck Master Tempo soak: PASS, zero frame
  drift and clipping. Documentation integrity and diff whitespace: PASS.
- Actual export with shortened analysis, MP3/WAV/FLAC tail seek and operator
  listening on JC4880/FLX4: **NOT RUN**.

## B5: Active-loop resize and exit

The imported MIT `audio_loop_resize` model from donor `428b97dd` identifies
where already-published PCM ceases to match changed loop bounds. The P4 decode
task withdraws only that unconsumed future under the existing ring/timeline
critical section, then resumes at the new start for a shortened loop or at the
old end when an extended or exited loop should continue through the file.
Multiple control changes before the decoder wakes retain the first old bounds.
An explicit user seek still flushes the ring and invalidates the pending cut.
For a loop shortened behind the audible playhead, the engine requests a seek
to the same phase inside the new bounds.

The canonical P4 timeline, output position and scratch ownership remain in
place. The imported model is tested with short loops that wrap many times
within the two-second decode runway. Exact-image operator A/B playback and
MAIN/cue listening remain a hardware acceptance gate.

Verification on 2026-10-04:

- Loop resize model: PASS across 69,920 simulated cases (47,936 cuts and
  11,362 playhead jumps), including X2, /2, changed IN, exit and short loops.
- Complete P4 host runner: PASS; ESP-IDF 6.0.2 P4 build: PASS. Application
  2,510,528 bytes, 1,159,488 below the 0x380000 budget; lock unchanged.
- Deterministic 300-second dual-deck Master Tempo soak: PASS, zero frame
  drift and clipping. Documentation integrity and diff whitespace: PASS.
- Physical loop resize while both decks play, scratch interaction, MAIN/cue
  listening and exact-image soak: **NOT RUN**.

## B6: Default-on load lock for local and remote requests

The donor's pure `deck_load_lock` verdict module is imported from `428b97dd`
under MIT. P4 `deck_core` owns a default-on switch and bases its decision on
the destination deck's actual audio transport state. Touch, controller and
Web Remote loads enter the same Library admission path. A rejected request
does not reserve the single-flight load slot or mutate the loaded deck. The
worker repeats the verdict after metadata resolution, immediately before its
existing deck reset and audio bind; PLAY during metadata loading therefore
rejects the pending load without replacing the current track. Web Remote
reports `409 Conflict` with an explicit load-lock message for an already
playing deck.

The switch has a core setter/getter for the future Settings page. This step
does not persist the switch in NVS or present a UI toggle, so the default-on
policy applies after reboot. CDJ-style held-CUE transport behavior and the
complete cue merge are separate remaining package B work.

Verification on 2026-10-04:

- Pure verdict and dual-deck core tests: PASS, including actual target-deck
  playback state, default-on policy and explicit switch-off behavior.
- Complete P4 host runner: PASS. UI simulator E2E: PASS; a rejected LOAD
  preserves the loaded track and all seven screenshot hashes remain unchanged.
- ESP-IDF 6.0.2 P4 build: PASS; application 2,510,944 bytes, 1,159,072
  below the `0x380000` budget; dependency lock unchanged.
- Real touch/FLX4/Web Remote load rejection, PLAY-during-worker race and
  operator audio confirmation on JC4880: **NOT RUN**.

## B7: Separate bounded memory cue import

ANLZ PCOB list type 0 now imports up to 16 memory points and loops into an
ordered metadata array independent of the eight hot-cue slots. A seventeenth
entry sets an explicit truncation flag. Points and loops at the same timestamp
remain separate. Malformed entries, including loops with an end before the
start, reject the DAT transaction before publication. Metadata snapshots carry
the inline memory cues unchanged.

This step does not merge imported cues with local edits, store deletions or
change transport controls; those remain package B work. Synthetic host cases
cover separation, export order, truncation, clone and transactional rejection.
Real Rekordbox memory cues and physical display/control acceptance: **NOT RUN**.

## B8: Source hot cues with persistent local overrides

The performance-pad bank now merges imported ANLZ hot cues with local edits by
the full 32-byte media identity. A v3 `hotcue_v3` record stores separate valid
and override masks: an override with no valid bit is a durable deletion of an
imported cue. The existing v2 namespace remains readable and its eight slots,
including empty ones, remain authoritative. New writes use v3; v2 data is not
deleted. `hot_cue_store_reset_to_source` can shadow either version with an
empty v3 override bank for the future explicit restore action.

Deck pad recall and LED presence now use the merged bank. The Hot Cues screen
uses the same source/local precedence when refreshing. Host tests cover v2
compatibility, v3 round-trip, deletion, restore semantics, full-ID collision,
source recall, local deletion and reload. The user-facing restore control,
touch/MIDI transport parity and physical Rekordbox cue check remain open.

## B9: Explicit source restore and cue refresh

Holding `HOLD RESTORE` on the Hot Cues screen queues a semantic source-restore
action for the selected deck. The deck task writes an empty v3 override bank,
refreshes cue LEDs for both decks using that identity, and advances an atomic
cue revision. The LVGL task notices that revision and refreshes the pad labels
after restore or a MIDI pad edit. Failed storage does not
advance the revision. A corrupt local record produces the same empty,
fail-closed bank in the UI and deck core.

The host regression deletes an imported cue, restores the source and recalls
its original position; release events cannot trigger restore. The 800x480
Hot Cues capture was visually reviewed before updating its baseline. Real
touch long press, NVS power interruption and FLX4 LED acceptance: **NOT RUN**.

## B10: Session-owned duration snapshot

The audio status API now publishes the fixed `analysis_span_ms`, the best
currently known `duration_ms` and the LOAD session generation which owns them.
All three come from the engine snapshot; stopped or failed decks expose no
old duration. Status does not acquire the lifecycle mutex or wait for loader
teardown. A WAV regression uses a 100 ms file with a deliberately wrong
120,000 ms analysis hint and verifies both values remain distinct, the session
matches, and STOP removes the old duration.

This is a duration-publication foundation. The remaining work is to consume
the live file duration in UI time/seek bounds while preserving the original
waveform/PVBR time base, refine valid PWV3 spans and measure MP3 EOF without
trusting an estimated seek coordinate. Physical duration/seek A/B: **NOT RUN**.

## B11: Shared hot-cue pad actions and imported loop recall

Firmware touch hot-cue pads now queue the same semantic pad press as the
controller. The deck core decides whether to recall an effective source/local
cue or store the current position in an empty slot. Both paths preserve the
current play/pause state; the touch path no longer automatically starts PLAY.
A loop cue recalls its start and activates its stored end; recalling a single
cue exits the active loop. Loop-adjust mode is cleared and its LEDs refreshed.

Host regressions cover imported loop bounds, single-cue loop exit, paused-state
preservation and second-deck isolation. The simulator keeps its explicit PC
transport adapter and verifies the shared presentation; physical touch/MIDI
parity, loop continuity and operator audio acceptance are **NOT RUN**. CDJ CUE
hold behavior and an explicit VINYL/CDJ jog mode remain separate pending work.
Waveform cue overlays still consume the source ANLZ bank; integrating local
overrides into the render snapshot remains an explicit UI/package H gate.

Verification on 2026-10-04 for B8-B11:

- Full P4 host runner and focused hot-cue, dual-deck/scratch and audio-engine
  suites: PASS. The production hot-cue serializer is now included in the runner.
- Shared 800x480 UI simulator and all 11 screenshot captures: PASS. Only the
  reviewed B9 Hot Cues baseline changed.
- ESP-IDF 6.0.2 JC4880 build: PASS; 2,533,344 bytes, 1,136,672 bytes below the
  `0x380000` application budget. Dependency lock unchanged.
- Documentation integrity and whitespace checks: PASS.
- Hardware smoke, installed image, physical soak and OTA: NOT RUN.

## B12: CDJ CUE press/release transport

CUE during PLAY returns to the stored cue and pauses. While paused away from
the cue, pressing CUE sets the current/quantized cue. Pressing at the cue
starts a held preview; releasing pauses and returns to that cue. PLAY during
preview commits playback so the later CUE release does not stop it. Repeated
presses and unmatched releases do not retrigger transport. Disconnect releases
an active preview, and deck reset/eject clears preview ownership.

Touch CUE sends PRESSED, RELEASED and PRESS_LOST through the same semantic
events as MIDI. The simulator retains its explicit transport adapter. Host
tests cover set, preview, duplicate press, release, PLAY commit, return from
PLAY, deck isolation and disconnect. Physical CUE feel and audible preroll:
**NOT RUN**.

Validation: full P4 host runner, both deck-core scratch configurations,
11 unchanged simulator baselines and documentation integrity passed.
ESP-IDF v6.0.2 P4 build passed: 2,533,696 bytes within the `0x380000`
application budget. Dependency lock unchanged; `git diff --check` passed.

## C1: Bounded optional PWV4 color preview

The existing ANLZ parser now reads PWV4 from `.EXT` after the required PWV3
waveform. It checks the 24-byte header, six-byte entry size, declared count and
section payload before allocating at most 7,200 bytes (1,200 columns). A larger
valid preview is explicitly marked truncated. Missing or malformed PWV4 is
ignored so a usable PWV3 waveform still loads. Re-parsing clears stale color;
metadata clones own their color bytes and `anlz_free` releases them. The format
was checked against donor `428b97dd`, but this step uses the current strict
section walker and transactional metadata lifecycle.

This is parser data only: no color waveform is rendered yet, and no playlist or
artwork work is claimed complete. Real exported `.EXT` files, visual inspection
and dual-deck hardware playback remain **NOT RUN**.

Verification on 2026-10-04:

- Focused ANLZ and library ANLZ suites: PASS, including valid, malformed,
  truncated, oversized, missing and clone cases.
- Complete P4 host runner: PASS (exit 0).
- ESP-IDF 6.0.2 P4 build: PASS; application 2,511,616 bytes, 1,158,400 below
  the `0x380000` budget; dependency lock unchanged.

## C2: PDB playlist and artwork table parsing

The bounded, page-at-a-time PDB parser now recognizes PlaylistTree (0x07),
PlaylistEntries (0x08) and Artwork (0x0D), as well as each track's artwork ID.
It retains up to 256 nodes, 8,192 entries and 1,024 artwork paths. The parser
keeps export entry order, validates playlist parents and rejects cycles,
duplicates, folder-as-playlist entries and missing track references. Artwork
paths must be absolute, fit the bounded buffer and avoid parent traversal.
Missing tables and rejected optional rows do not prevent track import;
truncation and invalid-row counts are reported in import stats. Media reads
remain in the existing 8 KiB-gated P4 path. The table layout is based on donor
`428b97dd` and adapted to the current parser.

These records are exposed through the PDB handle but are not yet published in
the live media catalog or displayed in Library. That integration is the next
part of package C. No real Rekordbox export or hardware was available for this
step: physical verification is **NOT RUN**.

## C3: Catalog publication and hierarchical Library browse

The local catalog now publishes playlist nodes, export-ordered track keys and
artwork paths in the same generation as its track records. Global sort changes
only the catalog row order; a playlist retains its exported track order.
Artwork path lookup requires the exact catalog generation. USB removal and
rebuild retire the playlist view, preventing stale keys from loading another
medium's track.

The existing 800x480 Library screen gains a PLAYLISTS/BACK button. Folder and
playlist rows open in place, and LOAD on a playlist song uses the existing
identity, load lock and worker path. Draw callbacks use a compact visible-page
key snapshot; they do no PDB or filesystem reads. A simulator fixture covers a
folder, its playlist, export order and three-step return to all tracks.

Verification on 2026-10-04: focused catalog tests, full P4 host runner,
ESP-IDF 6.0.2 P4 build and ten UI simulator screenshot/navigation checks
passed. Application size was 2,517,632 bytes, 1,152,384 below the `0x380000`
budget; the dependency lock did not change. The changed Library screenshots
were visually reviewed before replacing their baselines. Real USB browse,
touch and playback: **NOT RUN**.

## C4: Bounded artwork worker and PWV4 presentation

The Library now shows 40x40 RGB565 covers for the visible rows. A low-priority
core-1 worker resolves an artwork path only for the current track key and
catalog generation, reads at most 64 KiB in 4 KiB media-gated chunks, probes
the JPEG header and decodes into a fixed 24-slot PSRAM thumbnail cache. An
epoch cancels old page requests. The LVGL task alone copies a completed cover
to its own row image buffer; a missing, oversized, corrupt or unsupported JPEG
does not block audio loading. The worker pauses while a track load is active.
Bounded queue/cache/read/decode statistics are available from the artwork API.

The existing Overview mini waveform now renders optional PWV4 heights with
band-based colors in its ten-color palette. PWAV/PWV3 remains the fallback
when PWV4 is absent, invalid or silent. Existing high-resolution waveform
and playback paths are unchanged. The JPEG probe, thumbnail decoder, worker
and fixture generator are adapted from donor `428b97dd` under the repository's
MIT license; the current P4 catalog identity and media gate replace donor
board/network dependencies.

The host runner covers malformed JPEG headers and PWV4 resampling. The 800x480
simulator decodes baseline, grayscale, progressive and truncated synthetic
JPEGs; it displays covers on Library rows and captures the PWV4 overview.
The changed screenshots were visually reviewed before updating the eleven-capture
baseline manifest. Full host runner, simulator, ESP-IDF 6.0.2 P4 build,
dependency-lock check and binary budget pass. Real USB artwork, removal during
decode, touch fluidity and dual-deck playback on JC4880: **NOT RUN**.

Package C is **software verified for the current JC4880 target**. JC1060 UI
and physical acceptance belong to later packages and remain **NOT RUN**.

## C5: Versioned metadata cache preserves PWV4 and memory cues

The existing SD track metadata cache omitted both optional PWV4 bytes and the
new memory cue list. A warm cache hit therefore produced different UI/cue
metadata from a cold parse. Cache format v4 includes bounded color data,
memory cue records and their truncation flags. It validates counts and lengths
before allocation; v3 entries are ignored and a fresh parse writes v4. A host
round-trip exercises hot cues, same-timestamp memory point/loop, PWV4 bytes,
waveform detail and source-file signature invalidation. Exact firmware build,
host and simulator gates remain required. SD media/cache behavior on hardware:
**NOT RUN**.

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
