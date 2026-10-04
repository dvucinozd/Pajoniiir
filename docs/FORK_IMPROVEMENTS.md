# Fork improvements integration

Work in progress on `codex/fork-improvements`, starting from
`05296b8e8a2a10e38b3af58bb1e8034fbe806519`. Production remains M2.4.
Donor: [kayrozen/Pajoniiir](https://github.com/kayrozen/Pajoniiir/tree/428b97dd4a175f03d3a172c8db9c4d5ed94195fb),
frozen at `428b97dd4a175f03d3a172c8db9c4d5ed94195fb` (v323).
The [2026-10-04 software handoff](validation/FORK_IMPROVEMENTS_HANDOFF_20261004.md)
records the pushed checkpoint and hardware gates left unrun.

## Scope and delivery status

E1 introduces `board_adapter` contracts and immutable board capabilities.
Shared audio/UI/startup code uses the neutral header; USB controller routing,
storage routing/recovery and FS PHY selection read board roles. JC4880 keeps
storage root 0/controller root 1, while donor JC1060 uses storage root 1/
controller root 0 (FS PHY0). Host tests cover both configurations. This is not
an enumeration-order heuristic or a claim that JC1060 hardware passed.
JC4880 IDF 6.0.2 build passes at 2,541,200 bytes; lock unchanged.
[E2 adds the JC1060 target](validation/FORK_IMPROVEMENTS_PACKAGE_E_SOFTWARE_20261004.md),
shared peripherals, native geometry, Ethernet startup and separate lock/CI.
Physical bring-up is NOT RUN; new layouts remain H.

One P4 core must serve JC4880/FLX4 and JC1060/DDJ-400. Ethernet Link belongs
only to JC1060; recording remains an opt-in experiment. APTA integration is
outside this branch. These are planned capabilities, not current support claims.

| Package | Scope | Current state |
| --- | --- | --- |
| A | Baseline host tests, independent functional suites, PDB title, PQTZ downbeat | Software verified; physical acceptance NOT RUN |
| B | Accurate seek, duration, loop resize, memory/local cues, load lock | B1-B19 software verified on JC4880; physical acceptance NOT RUN |
| C | Hierarchical playlists, bounded artwork, PWV4 | C1-C5 software verified on JC4880 build and 800x480 simulator; physical acceptance NOT RUN |
| D | S3CP v4, all validators/compiler/exporter, DDJ-400 profile | Software verified; DDJ-400 hardware acceptance NOT RUN; UAC belongs to F |
| E | Board adapter, JC1060 entrypoint/BSP, dependency lock and CI | E1/E2 software verified; both clean container builds and host/simulator CI PASS on ecca351; hardware NOT RUN |
| F | Bounded UAC formats, MAIN routing, consumer-paced USB audio | Software verified; host and both clean build CI jobs PASS on 51ac8da; physical audio/reconnect acceptance NOT RUN |
| G | IDF 6.0.2 SD idle workaround, measurements, experimental recorder | Software verified on d902510; all six CI jobs PASS; experiments default-off; physical SD/audio/power-loss gates NOT RUN |
| H | Previous product design with new features at 800x480 and 1024x600 | Software verified; one selected presentation/shared Settings; allocation/stack/resource gates. H3 historical hardware FAIL recovered; final physical acceptance NOT RUN |
| I | MIT djlink codec, Ethernet discovery/claim/browse | Pending |
| J | Full persistent media identity, cancellable download, atomic cache | Pending |
| K | Epoch-bound network clock, controlled sync and tempo master | Pending |
| L | Project-bound OTA, documentation, qualification and release candidate | Pending |

Software verification, hardware acceptance and release are separate states.
The [package B software closure](validation/FORK_IMPROVEMENTS_PACKAGE_B_SOFTWARE_20261004.md)
records the final bounds, regressions and unrun acceptance matrix. Earlier
step descriptions below retain their historical pending items; later B steps
supersede them. UI redesign/presentation belongs to H and S3CP v4 to D.
JC1060, DDJ-400 and real CDJ peer acceptance remain **NOT RUN** until the
hardware is available. No deployment, production channel change or hardware
acceptance follows from a successful host test or build.

## Package G

Package G follows F with a shared Apache-2.0 SD idle component, internal SD bounce
policy, bounded storage diagnostics and fail-closed recorder ownership/recovery.
[G evidence and measurement procedure](validation/FORK_IMPROVEMENTS_PACKAGE_G_SOFTWARE_20261004.md)
separate software results from unrun card/audio gates. Ordinary firmware and
production OTA packaging exclude storage experiments. COM-connected devices
belonging to other projects are ignored; no device is contacted by this work.

## Package F

[F software closure](validation/FORK_IMPROVEMENTS_PACKAGE_F_SOFTWARE_20261004.md)
records descriptor bounds, actual 16/24-bit ISO packing, consumer-paced USB
MAIN/cue, stopped-only service routing, EP0 timeout ownership and reconnect
regressions. JC4880 keeps its PCM5102A default and qualified FLX4 mirror path;
JC1060 uses USB output with ES8311 disabled. Both IDF builds and host/simulator
checks pass; every physical audio/reconnect/latency gate remains NOT RUN.
Recording stays experimental/disabled and next work is G. No image was installed
or released by this package.

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
hold behavior is covered by B12; VINYL/CDJ core behavior is covered by B13a.
At B11, waveform cue overlays still consumed the source ANLZ bank. The subsequent
[H software closure](validation/FORK_IMPROVEMENTS_PACKAGE_H_SOFTWARE_20261004.md)
merges local overrides/tombstones into the owned product render snapshot and
checks deletion in actual Overview pixels; physical acceptance remains NOT RUN.

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

## B13a: VINYL/CDJ semantic jog core

Two idempotent semantic actions, `JOG_VINYL` and `JOG_CDJ`, select a mode
independently for each deck. Releases do not change the mode. VINYL remains
the startup default, preserving FLX4 behavior. Switching to CDJ releases an
active scratch or fallback platter hold before ignoring platter-top touch.
While playing, platter and bend-ring deltas nudge tempo; while paused, they
scrub. Loop-adjust ownership remains unchanged.

The read-only state snapshot exposes `jog_cdj_mode`. Track reset preserves
the current choice; reboot starts in VINYL. No NVS persistence is introduced.
The common semantic path is ready for touch and controller producers, but
the visible mode selector and profile binding remain pending. This core
extension alone does not make the mode selectable on the installed product.
B13b below supplies the touch producer; profile binding remains separate.

Host tests cover mode switching during active scratch/hold, repeated requests,
ignored releases/top-touch in CDJ, playing nudge, paused scrub, deck isolation,
track reset and return to VINYL, with scratch enabled and disabled.
Physical mode switching, audible behavior and exact-image soak: **NOT RUN**.

Validation: full P4 host runner and both scratch configurations passed;
11 simulator baselines unchanged; documentation integrity and diff checks
passed. ESP-IDF v6.0.2 build: 2,533,824 bytes; dependency lock unchanged.

## B13b: Active-deck touch jog mode selector

Hot Cues replaces the informational D1/D2 tile with a `D1 JOG: VINYL` /
`D2 JOG: CDJ` button. It reads the authoritative active-deck snapshot and
queues the same idempotent actions used by controller producers. The label
refreshes in the LVGL task on mode or target changes; unchanged frames do not
rewrite the label. A failed queue request logs an error and leaves the label
at the accepted state.

The PC event adapter now publishes the state snapshot after each applied
event, matching the real deck task before its next queue wait. Simulator
interactions cover touch change on D2, D1 isolation, target navigation and
label refresh after a controller semantic event. The Hot Cues screenshot
was visually reviewed; only its baseline changed, to `c1bb83e3...`.
Physical touch/FLX4 mode acceptance and audible transition: **NOT RUN**.

Validation: full P4 host runner passed; simulator interaction checks and all
11 screenshot hashes passed after visual review of Hot Cues. ESP-IDF v6.0.2
P4 build passed (2,534,384 bytes); dependency lock unchanged. Documentation
integrity and `git diff --check` passed. No image was installed or released.

## B14: Session-bound playback duration and fixed analysis time base

The UI retains the audio session generation returned by the accepted load.
It uses live file duration only when status is loaded, the captured session
matches, and duration is nonzero; otherwise metadata remains the fallback.
Rejecting or clearing a load cannot import duration from the preceding or
replacement audio session. The resolver performs no lifecycle wait or I/O.

Frame snapshots carry separate playback duration and analysis span. Overview
time, interpolation, progress and touch seek bounds use playback duration.
Zoom waveform sampling/cache keys continue to use analysis span. The mini
waveform and PWV4 are mapped onto the file timeline; an unanalyzed file tail
stays blank, and a shorter decoded file shows only its analyzed portion.
Duration changes invalidate time/cue positions and the mini canvas in the
LVGL task. Original beat timestamps, cue positions and PVBR tables do not
change. Metadata browsing rows retain their exported duration.

Host tests cover shorter/longer files, unknown length, unloaded status,
unbound/wrong sessions and integer boundaries. Renderer tests cover blank
analysis tails, cropped analysis and invalid stride. Simulator exercises the
production duration getter, a visible Overview update, fixed analysis span,
stale-session rejection and unloaded fallback; existing screenshot baselines
remain unchanged. The audio stub now mirrors the B10 status fields.

This is UI integration of B10, not a claim of measured MP3 EOF or precise
PWV3 span qualification. Deck-core beat-jump bounds still use loaded metadata
and require the same session association (implemented below in B15). Physical duration,
seek, waveform timing and dual-deck acceptance: **NOT RUN**.

Validation: new `ui_track_duration`, renderer span cases, full P4 host runner
and simulator interactions passed; all 11 screenshot baselines unchanged.
ESP-IDF v6.0.2 P4 build passed: 2,535,280 bytes within `0x380000`.
Dependency lock unchanged. Documentation integrity and diff checks passed.

## B15: Session-bound beat-jump and search limits

The immutable loaded-track payload/summary now records the exact accepted
audio session generation. The new `deck_core_publish_loaded_track_session`
API validates nonzero sessions against loaded audio status without taking
the lifecycle lock. A wrong or unloaded session is rejected before metadata
replacement. The original publish API remains metadata-only with session 0.
The production loader and simulator use session-bound publication; a failed
production publication retires only the originating audio session.

Beat-jump and shift+jog search choose live duration only for the same loaded
session with a known length. Otherwise they retain metadata bounds. Targets
are capped at the last millisecond before known EOF, and search saturates
at `UINT32_MAX` when length is unknown instead of wrapping. Source analysis,
beatgrid, cue positions and persistent identity remain unchanged. Clearing
a track clears the session association. No persistent or wire format changes.

Host tests cover longer audio, shorter audio, stale/unloaded publication,
legacy publication, D1/D2 isolation, backward jump at zero, a one-millisecond
track, unknown duration and integer overflow. Store tests verify coherent
session copying/reset, alongside the existing concurrent replacement suite.
MP3 EOF measurement and PWV3 span refinement remain open. Physical navigation,
cue/loop continuity and exact-image audio acceptance: **NOT RUN**.

Validation: full P4 host runner, final dual-deck tests in both scratch modes,
and loaded-track store suite passed. Simulator interactions and all 11
unchanged screenshot baselines passed. Final ESP-IDF v6.0.2 P4 build:
2,535,536 bytes within `0x380000`; dependency lock unchanged. Documentation
integrity and diff checks passed. No hardware image was installed or released.

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

## B16: Validated PWV3 analysis timing

The parser publishes a separate timing scalar only for a complete PWV3 payload
with one-byte entries, matching entry count and the 150 Hz rate marker. Legacy,
malformed or memory-capped payloads retain waveform fallback without claiming
precise duration. The donor `428b97dd` timing selection (MIT) is adapted to the
current metadata model: a missing PDB span accepts validated PWV3 timing;
otherwise the difference must be at most 1,500 ms. PDB duration is preserved.

The accepted analysis span travels with the generation-bound loaded track to
the audio seek/PVBR time base and waveform UI. Playback length still comes from
the bound audio session. This does not add an analysis-based audio EOF cutoff
or claim measured MP3 EOF for files without reliable duration metadata.

Metadata cache format v5 preserves timing on full and compact reads and rejects
inconsistent timing on save/load. Existing v4 entries in the same cache root
are rejected and regenerated from ANLZ; rollback firmware similarly regenerates
its own format. Cue edit namespaces, profile ABI, partitions and OTA are unchanged.
Parser tests cover invalid headers, exact memory cap and the 1,500 ms boundary;
library tests preserve PDB duration and retire stale timing. Cache tests cover
full/compact round-trip and invalid saves. Physical cold/warm cache, seek and
waveform acceptance: **NOT RUN**.

Full P4 host runner, all eleven unchanged simulator screenshots, ESP-IDF 6.0.2
P4 build and documentation integrity pass. The image is 2,536,304 bytes against
the unchanged `0x380000` budget; the dependency lock is unchanged. These are
software results; heap/stack and audio timing qualification on hardware remain
**NOT RUN**.

## B17: Sequential MP3 decoder EOF measurement

Each loaded audio session counts MP3 decoder output frames independently of
the UI position, seek base and analysis span. While decoding continuously from
the known file start, the best known playback length grows if the decoded
audio passes the metadata duration. At natural file EOF it becomes the measured
frame duration, including correction of overlong metadata. The analysis span
remains fixed and the existing output-drain policy still owns transport finish.
No audio is cut off at the analysis end and this measurement does not seek or
move the audible timeline. This integration is original code around the
previously imported `428b97dd` (MIT) track-length module.

A seek withdraws measurement authority immediately. The decode worker also
invalidates it on nonzero loop/user repositioning; a real zero restart begins
a new count from the audio start rather than the first legacy PVBR entry.
Rate changes and counter overflow invalidate the count. A media read failure
does not publish measured EOF. Unanchored seeks retain existing duration
fallback; exact duration for such a session would require a separately bounded
frame index/scan and is not claimed here. Controller jog-mode profile bindings
remain package D work, so package B is not declared complete.

Tests decode synthetic silent CBR and varying-bitrate MP3 with no Xing/VBRI,
with/without ID3v2, and trailing non-audio data. Both shorter and longer
metadata spans converge to the actual output WAV frame count. A partial count
followed by seek cannot overwrite fallback length. Pure tests cover reset,
discontinuity, sample-rate change and overflow. The PC export helper now skips
non-audio decoder iterations until real EOF rather than stopping at the first
zero-output iteration. Persistent formats, partitions, profile ABI and OTA
are unchanged; source rollback restores the preceding duration behavior.
Physical end-of-track, scratch, MAIN/cue, read-fault and exact-image soak gates
remain **NOT RUN**.

Full P4 host runner, eleven unchanged simulator screenshots, the 300-second
deterministic dual-deck Master Tempo host soak, ESP-IDF 6.0.2 P4 build and
documentation integrity pass. The application is 2,536,944 bytes within the
unchanged `0x380000` budget, with no dependency-lock change. Host soak results
do not qualify P4 CPU/audio deadlines or audible hardware performance.

## B18: Profile producers for VINYL/CDJ jog mode

The profile compiler now accepts `jog_vinyl`, `jog_cdj` and the existing
`restore_source_cues` deck actions using the established NOTE_VALUE packed
press/release representation. Explicit mode selection is idempotent, shares
the B13 core actions with touch, and does not require the package D v4 ABI.
Compiler tests verify both deck addresses and oldest-version v2 output;
runtime parser tests check both edges for each mode. FLX4 input/snapshot/LED
golden parity remains unchanged. Existing profile files are not rewritten.
Rollback retains the original compiler/profile files; new actions require the
development firmware and are not claimed implemented in M2.4. Physical mode
selection and reconnect tests: **NOT RUN**.

## B19: Complete MP3 frame index and package B software closure

A session-local index counts the full MPEG Layer III header stream and retains
at most 256 sparse checkpoints (about 2 KiB per deck). Checkpoints are decimated
as the file grows; frame numbers remain exact. It works without Xing, VBRI or
PVBR and does not infer audio length from PDB/analysis rounding. A seek walks
at most one checkpoint stride of headers to an exact MPEG frame with reservoir
lead, then discards PCM to the requested source sample. A valid frame which
produces no PCM during reservoir refill also consumes its share of the skip.
Loop wraps and paused cue preroll use the same indexed source path. The fixed
analysis/waveform span remains independent of the indexed file length.

The index also records the audio end before validated ID3v1/zero padding. This
avoids minimp3 rejecting the final audio frame when its successor is a tag.
Host decoding and seeking now preserve that final frame. Existing PVBR remains
the fallback for files without a complete index. Free-format MP3, broken or
mixed-rate header streams and unsupported trailing tags do not gain an exact
index claim. The index admits at most 1 GiB / 1,000,000 MPEG frames; firmware
build IO has a 30-second budget and seek lookup a 2-second budget. These are
defensive limits, not measured hardware latency promises. Incomplete/cancelled
scans are never published; an established index whose seek IO fails reports
`SEEK INDEX ERR` rather than silently substituting an estimated seek.

Firmware index IO runs in the decode worker outside AE_LOCK through the
existing page cache/media gate, yields every 128 reads, checks the load session
and stop flag, and reports scan progress. Results are rechecked under AE_LOCK;
seek results additionally match the current request/reason/preroll target.
STOP still joins the owning worker before file/index reuse. There are no
filesystem operations or allocations in the output/audio mixing task.
This is original integration around the MIT `428b97dd` parser/seek module.

Tests cover full counts, checkpoint compaction, exact start/forward/backward/
tail sample skips for CBR/VBR, ID3v2/v1, truncated/broken/mixed-rate streams,
cancel/read failure, file size limits and independence from misleading metadata.
Full host, unchanged simulator screenshots, Master Tempo host soak and IDF 6.0.2
build gates qualify software only. Physical latency, resource headroom, real
MP3/WAV/FLAC listening, scratch/loops, removal/reconnect, OTA and 180-minute
exact-image acceptance remain **NOT RUN**. No persistent format changes are
introduced by B19; source rollback discards the session index. B16 cache v5
regenerates safely across rollback, and original v2 cue records are retained.

Final B19 development build: 2,539,856 bytes (1,130,160 bytes below `0x380000`),
ESP-IDF 6.0.2 with the unchanged P4 dependency lock. This build is not a signed
release or an installed-device qualification.

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
