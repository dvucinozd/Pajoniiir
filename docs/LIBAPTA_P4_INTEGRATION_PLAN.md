# libapta-audio 1.1 Integration Plan For ESP32-P4

- Status: **deferred implementation plan**
- Recorded: **2026-08-22**
- Target: `firmware/main-deck-p4` on ESP-IDF **v6.0.2**

This document is the implementation plan for making libapta-audio 1.1 the
analysis and interchange foundation of the Pajoniiir P4 media library. It is
deliberately recorded before implementation so the current Rekordbox path can
remain stable while libapta-audio finishes its 1.1 algorithms, embedded profile
and release qualification.

This is not a statement that APTA support exists in current firmware. Current
`master` remains Rekordbox/PDB/ANLZ based. Implementation must not begin from a
moving development branch or be advertised until the gates in this document
pass.

## 1. Entry gate and upstream pin

The upstream `libapta-audio` development branch currently has its first four
infrastructure tasks complete through commit
[`c85c545`](https://github.com/dvucinozd/libapta-audio/commit/c85c54593797a54fce98933ad750749b0fecfd07):

1. APTA 1.1 key, meter/downbeat and calibrated-quality result views;
2. the validated external-result builder;
3. optional container-v1 `MKEY`, `MTRD` and `CONF` sections;
4. bounded streaming serialization and selective parsing.

The P4 production integration starts only after libapta-audio has also
completed and qualified:

- tempo/grid ensemble work for half, double, third and related metrical errors;
- calibrated confidence on a fully separate frozen holdout;
- native meter/downbeat detection;
- native global musical-key detection;
- progressive quick-pass/full-pass immutable result publication;
- the ESP32-P4 DJ memory profile and 1/5/30-minute memory probes;
- ESP-IDF 6.0.2/P4 CI, final API/ABI freeze and a published `v1.1.0` tag.

At integration time, add libapta as a Git dependency in
`firmware/main-deck-p4/main/idf_component.yml`. Pin `version` to the full
40-character release commit, not `main`, the `1.1.0` branch or a mutable tag
name. Commit the resulting `firmware/main-deck-p4/dependencies.lock` and retain
the CI check that a clean build does not rewrite it. The component-manager
syntax and Git-reference behavior are defined by the
[official manifest documentation](https://docs.espressif.com/projects/idf-component-manager/en/latest/reference/manifest_file.html#git-dependencies).

The manifest shape will be finalized against the released component name. The
intended policy is:

```yaml
dependencies:
  idf:
    version: "==6.0.2"
  libapta:
    git: https://github.com/dvucinozd/libapta-audio.git
    version: <full-v1.1.0-release-commit-sha>
```

Use a local `override_path` only for short-lived development. It must never be
present in a release commit or `dependencies.lock`.

## 2. Locked product boundary

libapta owns:

- PCM analysis and progressive scheduling;
- waveform overview/detail results;
- tempo candidates, global/local grid and dynamic-tempo results;
- musical key, meter/downbeat and calibrated per-feature quality;
- immutable result generations;
- validated import through `apta_result_builder_t`;
- deterministic `.apta` serialization and bounded parsing.

The P4 remains the sole owner of:

- MP3/WAV/FLAC codecs and seek mechanics;
- USB/FAT32/exFAT mounting, files, directories and transactions;
- media identity, catalog, playlists and tag parsing;
- hot cues and loops;
- decoder seek indexes, including imported Rekordbox PVBR;
- deck, playback, mixer, effects and audio-position state;
- analyzer task scheduling around real-time product pressure;
- all LVGL state and presentation.

The S3 and the existing `0xA5` control link do not change. No analysis state is
moved to the controller board. P4 remains authoritative for deck/playback/mixer
state and LED decisions.

Locked scope:

- formats: MP3, WAV and FLAC;
- maximum full-analysis duration: 30 minutes;
- maximum catalog size: 10,000 tracks;
- one active native analysis at a time;
- sidecars and M3U8 playlists live on the USB medium;
- write failure never blocks ordinary playback;
- no manual BPM/grid/downbeat/key editor in this phase;
- loudness, true peak, autogain and phrase analysis remain out of scope;
- Rekordbox stays supported as an importer, but ordinary folders plus APTA
  become the authoritative new-library path after rollout.

## 3. Feature flag and rollback boundary

Introduce `CONFIG_PAJONIIIR_APTA_LIBRARY` in a P4 project-level Kconfig file.
The first implementation commits keep it disabled by default.

When disabled:

- the existing PDB/ANLZ library, deck load and Sync behavior must compile and
  behave exactly as before;
- no libapta analyzer task starts;
- no `/PAJONIIIR` sidecar directory is created;
- existing host and UI simulator baselines remain unchanged.

When enabled, the new catalog and neutral analysis model are used, while the
Rekordbox parser is retained as an input provider. Rollback is a configuration
change, not a media conversion: old firmware ignores `/PAJONIIIR`, and new
firmware can rebuild every derived file from source audio or Rekordbox data.

Set the feature on by default only after the complete hardware gate passes.
Remove the direct ANLZ consumer path only after at least one release line has
shipped with the feature flag and parity coverage.

## 4. Neutral P4 analysis model

The first code migration is to stop exposing `anlz_metadata_t` outside the
Rekordbox importer. Add a P4-owned `track_analysis` component with an immutable,
reference-counted `track_analysis_t` snapshot.

It must expose provider-neutral views for:

- source frame rate, duration and source fingerprint;
- waveform overview and detail-tile handles;
- selected BPM and candidates;
- global/local beatgrid and explicit beat lookup;
- meter/downbeat segments;
- musical key and candidates;
- calibrated per-feature quality and warning flags;
- result generation, provider and provenance.

Provider values should distinguish at least:

- `TRACK_ANALYSIS_PROVIDER_APTA_CACHE`;
- `TRACK_ANALYSIS_PROVIDER_APTA_NATIVE`;
- `TRACK_ANALYSIS_PROVIDER_REKORDBOX_IMPORT`.

The neutral snapshot may retain an `apta_result_t`, but no deck, UI, Beat Jump
or Sync header may include libapta or Rekordbox parser types. The adapter owns
all frame-to-millisecond conversions and validates that they cannot overflow.

Hot cues and seek tables are deliberately not fields in `track_analysis_t`.
Deck load combines three independent owned objects:

1. audio source/decoder and `pjsi` seek data;
2. immutable `track_analysis_t`;
3. P4 hot-cue state.

Migrate these current consumers from `anlz_metadata_t` to the neutral model:

- `beat_jump`;
- `deck_core` and `deck_loaded_track_store`;
- `media_catalog` loaded-track payloads;
- Sync and quantized loop/transport helpers;
- `ui_frame_context`, `ui_deck_anlz_store`, Overview waveform/grid renderers,
  beat indicator and performance tabs.

Keep publication immutable. A quick-pass result and a later full result are
different generations; workers never mutate an object already visible to a
deck or the UI. A playing deck pins the generation used for quantized transport
until a validated compatible upgrade is adopted at a safe boundary or the next
load. UI waveform refinement may use a newer acquired generation independently.

## 5. Media identity and compact catalog

Replace the Rekordbox-only 32-bit row identity with:

```c
typedef struct {
    uint8_t bytes[16];
} media_track_id_t;
```

Derive it deterministically from:

- a stable USB-volume identity: partition GUID when present, otherwise the
  filesystem volume serial plus available USB device identity;
- the normalized UTF-8 path relative to the volume root.

The path canonicalizer is a separately host-tested component. It converts
separators to `/`, removes redundant `.` elements, rejects escaping `..`,
applies the documented FAT/exFAT case policy and produces the same bytes on P4
and host. Track IDs are rendered as 32 lowercase hexadecimal characters in
sidecar filenames.

Every catalog and sidecar record also stores the full SHA-256 of source audio
bytes. Identity tracks a media location; SHA-256 invalidates analysis, seek and
state data when content is replaced at that location.

The catalog supports 10,000 tracks without embedding large arrays or repeated
strings in each row. Use:

- fixed-size compact rows;
- a bounded UTF-8 string pool for paths, title, artist and album;
- compact order/index buffers for sort and filtering;
- handles or offsets to lazy waveform, grid and seek sidecars;
- generation-based transactional publication, preserving the current stale-load
  protection.

Do not place 400-column waveforms, 400-entry PVBR tables, beat arrays or full
paths inline in every catalog row. The existing `library_track_t` and
`media_loaded_track_t` layouts are migration inputs, not the target layout.

## 6. Folder scanner and tag readers

Add a recursive scanner for MP3, WAV and FLAC. It starts at the mounted volume
root, skips `/PIONEER` analysis internals and `/PAJONIIIR` sidecars, uses bounded
path/depth/entry limits and continues past unreadable or corrupt files with a
diagnostic counter.

Implement bounded, host-testable readers for:

- ID3v2.3 and ID3v2.4, including synchsafe sizes, extended headers and
  unsynchronization bounds;
- FLAC Vorbis Comments;
- WAV RIFF/INFO chunks with checked chunk sizes and padding.

The first catalog contract needs title, artist and album. Missing tags fall back
to the filename and empty optional fields. Decoder metadata supplies duration,
sample rate and channels. BPM/key text tags are not treated as Sync-ready APTA
analysis.

At mount:

1. read a valid `/PAJONIIIR/catalog.v1` immediately and publish it;
2. asynchronously scan and compare normalized path, size and mtime;
3. calculate SHA-256 only for new, changed, ambiguous or verification-due
   records rather than hashing all 10,000 files before the Library appears;
4. transactionally publish and save a new catalog generation;
5. retire removed tracks and invalidate replaced content.

Zero-track media is valid. Duplicate filenames in different directories remain
distinct. Unicode, malformed tags, deep directories and disconnect during scan
must not corrupt the last published catalog.

## 7. USB sidecar layout

Use this versioned layout:

```text
/PAJONIIIR/catalog.v1
/PAJONIIIR/analysis/<track-id>.apta
/PAJONIIIR/seek/<track-id>.pjsi
/PAJONIIIR/state/<track-id>.bin
/PAJONIIIR/playlists/<playlist-name>.m3u8
```

File roles:

- `.apta`: libapta-owned container bytes, read/written through streaming APIs;
- `.pjsi`: P4 decoder seek index, including imported PVBR where applicable;
- state `.bin`: P4 hot cues/loops, source SHA-256 and versioned CRC;
- M3U8: relative UTF-8 audio paths, one path per entry;
- `catalog.v1`: compact rows/string-pool metadata and validation fingerprints.

All P4-owned binary files use magic, version, bounded counts/sizes, source
SHA-256 and CRC before publication. Unknown future versions are ignored and
rebuilt, never partially interpreted.

The current `/sd/trackcache/<track-key>/meta.bin` cache is a legacy ANLZ
optimization, not an APTA sidecar and not a trusted migration source. Keep it
only for the disabled rollback configuration while parity is established. The
enabled path must not dual-write it; remove the component dependency after the
APTA/Rekordbox importer path has shipped and rollback support is retired.

Every material write follows:

1. check mount generation, write protection and free space;
2. create/truncate a sibling `.part` file;
3. write bounded chunks through `media_io_gate`;
4. flush the file and filesystem;
5. issue real SCSI `SYNCHRONIZE CACHE` for `CTRL_SYNC` instead of the current
   unconditional success stub;
6. close and rename `.part` to the final path;
7. publish success only if mount generation still matches.

On mount, recover or delete recognized stale `.part` files using age, header,
CRC and final-file presence. Fault-inject allocation, open, every write, flush,
SCSI sync, close, rename and disconnect boundaries. If media is read-only,
full, removed or rejects synchronization, retain a bounded temporary RAM result
and continue playback. Never wait for a sidecar write before starting audio.

## 8. M3U8 playlists

Support create, rename, delete, add, remove and reorder. Store normalized paths
relative to the USB root in UTF-8. Reject absolute paths and escaping `..`.

Playlist commands execute as transactions on a worker-owned model. The worker
may parse, validate and prepare a new snapshot, but only `ui_update()` in the
LVGL task may publish Library widgets or change selection. A missing track is a
retained unresolved entry with a visible diagnostic, not a parser crash or an
implicit destructive rewrite.

Playlist writes use the same `.part`, flush, device-sync and rename path as all
other sidecars.

## 9. Rekordbox importer

Retain `rekordbox_pdb` and `rekordbox_anlz` as compatibility providers, not as
the application data model.

Map data as follows:

| Rekordbox data | Destination |
|---|---|
| PDB title/artist/album/path/duration/key | compact P4 catalog and, where valid, imported key input |
| PQTZ BPM and beats | APTA tempo/global grid and meter/downbeat builder inputs |
| PWAV/PWV3 | APTA overview/detail waveform builder inputs |
| PVBR | P4 `.pjsi` seek cache only |
| PCOB cues/loops | P4 hot-cue store only |

Create an importer that validates ANLZ/PDB fields, converts time units to source
frames with checked arithmetic, fills `APTA_RESULT_PROVENANCE_EXTERNAL_IMPORT`
and finalizes through `apta_result_builder_t`. It must not invoke analysis again
or create a private APTA result representation.

Do not invent calibrated quality. Rekordbox-imported data becomes Sync-ready
only after an importer calibration/model policy is explicitly validated against
the parity fixtures and the required BPM/grid/meter data is complete. Missing,
inconsistent or assumed fields receive a warning or remain unavailable, which
keeps Sync disabled while playback remains normal.

Golden parity fixtures must compare existing ANLZ behavior with the neutral
result for BPM, every beat position/ordinal, waveform sampling, downbeat, cue
separation and seek-table separation.

## 10. Native analyzer runtime

First track load follows this order:

1. validate identity and open the audio source;
2. start playback immediately using the existing decoder/cache path;
3. load a SHA-matching `.apta` result if present;
4. otherwise import valid Rekordbox data when available;
5. otherwise queue native analysis;
6. publish quick-pass and full-pass immutable generations as they become valid;
7. transactionally store the full `.apta` result when USB policy permits.

Only one analyzer session may be active. Pin its task to core 1 below the LVGL
task priority; audio loader, decode and output remain on core 0. Feed PCM through
a separate decoder/source instance so analysis cannot consume or seek the
playing deck decoder.

The analyzer must cooperatively pause when any of these gates is active:

- either deck audio-cache watermark is below the configured safe level;
- scratch/vinyl work is active or imminent;
- recording I/O is active if the recorder is re-enabled;
- high-priority USB/media I/O is queued;
- LVGL frame/DSI health reports sustained pressure;
- USB mount generation changes or the source disconnects;
- internal RAM or PSRAM crosses the configured reserve.

Resume from retained bounded state. Cancellation and disconnect release all
session, decoder and scratch resources without publishing a partial object as a
final result.

## 11. P4 DJ memory profile

The release profile is a hard contract, not a target average:

| Resource | Maximum |
|---|---:|
| Full-analysis duration | 30 minutes |
| Explicit beats | 9,216 |
| Overview columns | 4,096 adaptive columns |
| Concurrent native analyses | 1 |
| Analyzer/session/result PSRAM | 6 MiB |
| Analyzer internal RAM | 128 KiB |
| Streaming scratch | 64 KiB |

Zoom detail uses bounded, evictable detail tiles rather than increasing the
overview array. Configure libapta capacities before session creation and reject
tracks outside the profile without unbounded fallback. Memory must stop growing
after configured capacities are reached; duration, number of processing calls
and repeated quick/full publication must not cause linear retained growth.

Use P4 allocator callbacks so large/persistent blocks go to PSRAM and explicitly
latency-sensitive blocks use internal RAM. Record current/peak bytes by memory
class and object type. Failure to allocate analysis memory degrades analysis,
not playback.

## 12. Sync and quantized-operation quality gate

Centralize one pure policy function used by Sync, Beat Jump, quantized loops and
the UI indicator. A result is Sync-ready only when:

- BPM is `FINAL`;
- global grid is `FINAL`;
- meter/downbeat is `FINAL`;
- each required calibrated quality record has confidence at least 95;
- no required quality record has `AMBIGUOUS`, `DEGRADED`, `OUT_OF_DOMAIN` or
  `DETECTOR_DISAGREEMENT`;
- source SHA-256 matches the loaded audio;
- the deck still owns the evaluated immutable generation.

When the gate fails, playback, pitch, scratch, cueing and non-quantized controls
continue normally. Sync and quantized actions reject deterministically, the UI
shows analysis pending/unavailable, and no caller implements a looser fallback.

The libapta frozen holdout must meet these upstream release criteria before P4
enables the gate by default:

- at least 95% of all tempo results within 1%;
- among Sync-ready results, at least 99% within 0.1%, at least 80% coverage and
  zero metrical-ratio errors;
- beatgrid median error at most 20 ms and p95 at most 50 ms;
- downbeat at least 98% correct among Sync-ready results;
- key tonic+mode at least 95% correct among actionable results with at least
  70% coverage.

## 13. Tasking and UI publication

Workers may scan, hash, parse tags, read/write sidecars, import results and run
analysis. They publish only immutable data snapshots and monotonic event
generations.

All playlist and Library UI changes continue to execute exclusively from
`ui_update()` in the LVGL task through the existing durable event-counter/command
pattern. Workers must not call LVGL, retain pointers into an old catalog
generation or publish mutable libapta views.

USB disconnect first advances the media generation and prevents new work, then
cancels analyzer/import tasks, retires catalog and analysis snapshots, and lets
the LVGL task apply the visible state. Deck/audio teardown remains P4-owned.

## 14. Ordered implementation phases

### P0. Upstream release and disabled dependency

- verify libapta `v1.1.0` release evidence and full commit SHA;
- pin the Git dependency and commit `dependencies.lock`;
- add `CONFIG_PAJONIIIR_APTA_LIBRARY=n` and a compile/link smoke;
- verify disabled builds and existing behavior are unchanged.

Exit: clean IDF 6.0.2 P4 build with a reproducible lockfile and no runtime path
change.

### P1. Neutral analysis model

- add immutable `track_analysis_t`, adapters and host tests;
- migrate Beat Jump, deck store, UI waveform/grid and Sync consumers;
- keep ANLZ as the only provider during this phase;
- remove `rekordbox_anlz.h` includes from migrated public headers.

Exit: ANLZ parity suite and UI simulator pass with no direct ANLZ application
consumer outside the importer boundary.

### P2. Identity, scanner, tags and compact catalog

- implement volume/path identity and SHA invalidation;
- add bounded scanner and three tag readers;
- replace large inline catalog rows with compact rows/string pool/handles;
- cover 0, 1 and 10,000 tracks plus Unicode and corrupt input.

Exit: catalog appears immediately from cache and refreshes transactionally
without blocking audio or LVGL.

### P3. Sidecars, playlists and USB write hardening

- implement `/PAJONIIIR` layout and versioned P4 file formats;
- add M3U8 operations;
- implement write-protect/free-space checks and real SCSI sync;
- add `.part` recovery and full write-boundary fault injection.

Exit: every write failure preserves playback and the previous valid state.

### P4. Rekordbox-to-APTA importer

- map PDB/ANLZ through `apta_result_builder_t`;
- split PVBR and PCOB into P4 stores;
- serialize imported APTA results and prove read-back parity;
- implement the explicit importer quality policy.

Exit: Rekordbox media works through the neutral/APTA path with fixture parity and
without app-level `anlz_metadata_t` coupling.

### P5. Native quick/full analysis

- add analyzer source adapter, task, allocator and pause/cancel gates;
- publish quick and full immutable generations;
- store full results transactionally;
- verify 1/5/30-minute resource plateaus.

Exit: first playback never waits for analysis and dual-deck audio deadlines stay
healthy during background work.

### P6. Quality policy and UI state

- centralize Sync-ready evaluation;
- disable quantized functions on non-actionable results;
- expose pending/degraded/final state without mutating UI from workers;
- add safe generation-upgrade behavior.

Exit: policy unit tests cover every state, confidence and warning combination;
the UI simulator covers pending, ready and degraded tracks.

### P7. Default rollout and legacy containment

- run the complete host/build/hardware matrix;
- set `CONFIG_PAJONIIIR_APTA_LIBRARY=y` by default;
- retain PDB/ANLZ only as importer modules;
- update product documentation and recovery procedures.

Exit: folder/APTA is authoritative, Rekordbox media remains compatible and the
legacy flag provides a tested rollback for one release line.

## 15. Verification matrix

Host tests must cover:

- empty, one-track and 10,000-track media;
- Unicode, duplicate names, deep folders and invalid path traversal;
- ID3v2.3/v2.4, Vorbis Comment and RIFF/INFO happy/corrupt/oversized fixtures;
- catalog and string-pool capacity edges;
- replaced content with unchanged path and changed SHA-256;
- corrupt/truncated/future `.apta`, `.pjsi`, state, catalog and M3U8 files;
- read-only, full and disconnected media;
- allocation failure at every owned object;
- write/flush/SCSI-sync/rename fault injection;
- ANLZ/APTA parity and deterministic builder output;
- quick/full generation concurrency and stale media-generation rejection;
- Sync quality policy truth table;
- UI command publication only from `ui_update()`.

Required automated gates:

```powershell
.\tests\run_p4_host_tests.ps1
.\tests\ui_simulator\run_ui_simulator_e2e.ps1
. C:\Espressif\tools\Microsoft.v6.0.2.PowerShell_profile.ps1
Set-Location firmware\main-deck-p4
idf.py build
```

Add dedicated libapta integration tests to `run_p4_host_tests.ps1`; do not rely
only on libapta's upstream suite.

Hardware acceptance uses both FAT32 and exFAT media and includes:

- ordinary folder scan and Rekordbox import;
- dual-deck MP3/WAV/FLAC playback while full analysis and sidecar writes run;
- scratch, Master Tempo and all Beat FX during analysis;
- low-cache, analyzer pause/resume and cancellation pressure;
- read-only, full, unplugged and reinserted USB behavior;
- deliberate stale `.part` recovery;
- 30-minute soak with zero I2S underruns, watchdogs, audio-cache starvation or
  significant LVGL cadence regression;
- measured PSRAM/internal/scratch peaks within the profile.

## 16. Completion checklist

The integration is complete only when all items are true:

- [ ] libapta `v1.1.0` is pinned by exact release commit and locked;
- [ ] no app consumer outside the importer includes `rekordbox_anlz.h`;
- [ ] 10,000-track catalog and all three tag readers pass host limits;
- [ ] sidecars and playlists survive the complete fault-injection matrix;
- [ ] playback starts without waiting for missing analysis;
- [ ] quick/full results are immutable, bounded and safely published;
- [ ] Sync/quantized operations share one strict quality gate;
- [ ] Rekordbox parity fixtures pass through the builder path;
- [ ] P4 1/5/30-minute memory probes plateau within budget;
- [ ] P4 host suite, UI simulator and clean IDF 6.0.2 build pass;
- [ ] FAT32/exFAT dual-deck hardware soak passes under analysis and writes;
- [ ] documentation reflects the enabled default and recovery behavior.

Until every row is closed, this document remains a future implementation plan
and the existing Rekordbox path remains the production behavior.
