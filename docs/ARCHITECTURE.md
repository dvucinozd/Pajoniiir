# Architecture

Status: **current M2.4 P4-only architecture, reconciled 2026-09-29**. The P4 is
both the authoritative playback/UI engine and the direct dual-root USB host.
No secondary firmware target or inter-board transport belongs to the product.

Development integration on `codex/fork-improvements` additionally binds
loaded-track snapshots to the audio session returned by the accepted LOAD.
UI duration, beat-jump and search consume live length only for that session;
metadata remains the fallback and waveform time base. See
[B14-B19 integration evidence](FORK_IMPROVEMENTS.md). A bounded session-local
MP3 frame index supplies file length and source-sample seek geometry without
rescaling the analysis waveform. Index IO is owned by the decoder worker
outside the output/audio lock; publication and seek commit recheck session and
request identity. This is software evidence,
with physical acceptance pending; the production baseline above is unchanged.

## High-Level Flow

L keeps signed board identity in the existing project field. The inactive app
descriptor must have bounded project/version text matching the running project
and signed version before selection. Ordinary candidate tooling additionally
checks clean/pushed source, exact green CI, fixed image budget and absence of
experiments. Candidate evidence is separate from the signed OTA schema and
records both dependency locks, configuration, binary hashes and NOT RUN physical
gates. JC1060 has a separate channel root; app-only OTA cannot migrate partitions.

Development package H constructs one selected Overview/Library/Hot Cues tree;
Settings and chrome are shared. Library ownership/actions do not depend on table
widgets. The product Overview retains leased analysis snapshots, owns PSRAM
render metadata, merges local cue edits by full persistent identity and copies
borrowed artwork before another cache operation. Markers reuse waveform buffers.
LVGL alone mutates widgets; workers own filesystem/JPEG operations. Paired holds
retain their deck and are cancelled by tab/target/screensaver changes.

`firmware_resources` records bounded allocation-failure/phase data from common
startup and self-sampled task stack minima without retaining task handles.
Output sampling is throttled and included in its deadline timing. The read-only
`/api/resources` supplements existing Web status without changing its fields.
Both ordinary and preview LVGL objects/styles/text now use the PSRAM-only
allocator and report owned bytes. Ordinary CLIB initially remained unchanged,
but the installed H candidate failed the internal largest-block reserve after
two paused loads. Waveform PPA source buffers also allocate directly in PSRAM;
HTTP USB status text is request-owned PSRAM rather than a 2 KiB stack array.
Audio and USB DMA placement remain unchanged. Historical CLIB measurements
report owned LVGL bytes as unknown rather than fabricated. These diagnostics
and [resource gates](validation/FORK_IMPROVEMENTS_PACKAGE_H_SOFTWARE_20261004.md)
require physical measurements before acceptance.

Development package G preserves the FAT and media locks, adding a long-operation
reservation shared by REC and future download workers. START/STOP serialize;
producer admission closes on loss, and timeout retains writer-owned resources.
The producer performs no filesystem work. SD idle yielding is a default-off
IDF 6.0.2 experiment; JC1060 PSRAM USB DMA requires internal SD bounce for both
read/write. Saturating SD transfer/gate snapshots are optional Web status fields.
See [G bounds and physical gates](validation/FORK_IMPROVEMENTS_PACKAGE_G_SOFTWARE_20261004.md).

Development package E adds `main-deck-jc1060` with an explicit shared-component
list and a thin wrapper around common P4 startup. `board_adapter` owns immutable
capabilities and shared touch/codec/SD peripherals; display BSP and Ethernet
startup remain board-specific. The JC4880 BSP is absent from the JC1060 build.
The core is not copied. Both signed-manifest and application descriptor checks
use the running project identity. See [E software evidence](validation/FORK_IMPROVEMENTS_PACKAGE_E_SOFTWARE_20261004.md);
new-board physical acceptance remains NOT RUN.

Package I isolates Link to the JC1060 Ethernet worker. A sans-I/O DBServer model
and strictly interface/local-IP-bound nonblocking adapter share one session and
one request. Its PSRAM cache owns at most 2,000 metadata rows; only bounded page
copies cross into LVGL. Source/claim/connection epochs and command IDs reject
stale publication. The Library owner handles navigation and incoming load
admission without network or filesystem work. Metadata-only LOAD cannot alter a
deck; verified local audio is required before acceptance/ACK. Local library
advertising and Wi-Fi Link are absent. See the
[I software closure](validation/FORK_IMPROVEMENTS_PACKAGE_I_SOFTWARE_20261005.md).

Package J runs NFS/PDB/analysis/JPEG work in the existing single-flight load
worker, with PSRAM-owned buffers, Ethernet-only UDP and the same serialized DB
session. DOWNLOAD reserves SD admission against REC. Filesystem reads/writes
yield the SD gate in at most 4 KiB chunks; close/finalize is serialized even after
cancellation. A full SHA-derived content identity binds audio/ANLZ/artwork/cues;
locator IDs never enter persistent storage. NFS volumes lack UUID evidence, so
each mount/load adds a random session nonce and never reuses an old session hit.
The cache publishes a checksummed completion manifest only after exact length,
flush/fsync/close and read-back hash match. Deck and active-artifact pins fence
pruning. LVGL receives owned metadata/artwork only after common LOAD admission.
The audio decoder gates /sd paths independently of local USB availability;
USB removal retires only USB sessions and preserves SD loading/output sessions.
No filesystem or network work enters the audio output task. See
[J evidence and limits](validation/FORK_IMPROVEMENTS_PACKAGE_J_SOFTWARE_20261005.md).

Package K exchanges bounded value snapshots between the Ethernet worker and
deck task. The network worker owns peer selection/handoff and sends packets;
the deck task alone runs the 40 ms sync model from the accepted audio position.
No network callback changes playback. A nonblocking lifecycle guard rechecks
the accepted audio session before pitch/seek, rejecting replaced loads/holds.
Source and master epochs reset phase filtering, never implicitly seek. Master
loss retains tempo in WAIT. Only explicit SYNC/PLAY arms a one-time alignment;
ordinary phase errors use at most 1 percent trim and cannot trigger periodic
resync. Own player identity is independent of unrelated peer inventory epochs.
Local-master handoff requires the current authority's reply; remote handoff
requires its announced target/source epoch and confirmation within three seconds.
Outgoing status and beats describe both claimed decks without advertising a
library. Sink/rate latency calibration defaults unmeasured and is RAM-only.
JC4880 registers no network provider, retaining its local sync path. See
[K evidence and physical gates](validation/FORK_IMPROVEMENTS_PACKAGE_K_SOFTWARE_20261005.md).

Package F separates the authoritative MAIN sink from USB cue mirroring.
JC4880 defaults to blocking PCM5102A I2S pacing; its FLX4 format, attenuation
and ring clock corrections remain unchanged. JC1060 defaults to USB MAIN 1/2
and cue 3/4, with ES8311 disabled. The single producer waits on actual UAC ring
space with a bounded, epoch-aware wait; USB pacing has no duplicate/trim or
second codec clock. Accepted USB blocks drive timeline bookkeeping. A failed
active write takes the existing fail-closed transport path.
Sink selection uses lifecycle admission and both deck locks, stops the output
producer before changing ring policy, and rejects PLAY/LOAD/scratch/recording.
UAC callback ownership remains with the USB owner; timed-out EP0 transfers are
retained until cancellation/detach and trigger root recovery. See
[F bounds and unrun physical gates](validation/FORK_IMPROVEMENTS_PACKAGE_F_SOFTWARE_20261004.md).

```text
Pioneer DDJ-FLX4
    |
    | USB1: MIDI + LEDs + four-channel UAC
    v
ESP32-P4 main-deck-p4
    |-- USB0: Rekordbox storage
    |-- PCM5102A: MAIN output
    `-- FLX4 UAC channels 3/4: cue/PFL output
```

## ESP32-P4 Responsibilities

The P4 remains authoritative for performance state.

Responsibilities:

- load Rekordbox tracks and analysis data from USB media;
- host the DDJ-FLX4 directly on USB1 and map MIDI to semantic events;
- activate SD/web controller profiles locally and send LED MIDI directly;
- stream cue/PFL audio directly to FLX4 UAC channels 3/4;
- own two `deck_core` state instances;
- own the mixer state: channel faders, crossfader, pregain, EQ/filter when
  implemented, cue/PFL selection;
- own controller behavior that changes playback state, including Hot Cue,
  Loop, Beat Jump, Tempo Range, and the current one-shot Beat Sync signed
  intra-beat phase-align behavior;
- decode audio and write master/cue buffers to hardware;
- render UI state;
- force a P4-owned LED snapshot after an FLX4 reconnect so physical LEDs
  recover from the authoritative state.

Current P4 audio ownership rule:

- each deck owns its own engine state, bounded-cache/source slot, decode runtime, PCM ring,
  resampler, lifecycle status, and last-error state;
- compressed audio (MP3/WAV/FLAC) uses a bounded LRU page cache
  (`audio_compressed_cache`, 8 × 32 KiB per deck) instead of loading the entire
  file into contiguous PSRAM. A cache miss performs one gated `read_at` from
  the source; FLAC uses `drflac_open` with seekable cache callbacks. The WAV
  decoder currently accepts classic RIFF/WAVE linear PCM16, mono or stereo,
  and rejects 24/32-bit PCM, IEEE float and `WAVE_FORMAT_EXTENSIBLE`;
- one shared firmware output service owns codec open/close and consumes both
  deck PCM rings through the output mixer;
- the LVGL task is pinned to CPU1, while the P4 audio loader, decode, and shared
  output tasks are pinned to CPU0 so UI rendering and real-time audio do not
  share the same core;
- when `CONFIG_BSP_PCM5102A_MAIN_OUT` is enabled, the shared output service
  reconfigures the PCM5102A I2S1 clock to the loaded track sample rate before
  starting playback. The release configuration disables the legacy ES8311
  monitor path;
- PCM5102 writes are bounded to one block period per driver call and at most
  three calls for a short write. The sink resumes only at the unwritten byte
  suffix, publishes call/short/timeout/error counters, and playback position is
  advanced only after every configured hardware sink accepts the block. A sink
  fault stops the output service in an explicit error state; STOP disables the
  PCM5102 channel to wake an in-flight write and the next LOAD re-enables it;
- the channel signal chain is explicit and remains single-precision wide until
  an output sink: source/resampler → channel TRIM/pregain → three-band EQ →
  channel filter/Pad FX/Beat FX → channel fader/crossfader → two-deck sum →
  controller master volume/software master trim → MAIN limiter → PCM sink.
  Effects do not clamp to `int16_t` internally. PFL branches from the same
  post-TRIM/post-DSP frame before channel fader/crossfader, so TRIM and EQ/FX
  affect cue level while channel fader and crossfader do not. The headphone
  path performs only its final PCM sink conversion; the master limiter remains
  MAIN-only;
- the audio engine exposes a non-boosting software master trim scalar
  (`0.0–1.0`, default `1.0`) after the two-deck sum and before the MAIN limiter.
  The P4
  Settings screen exposes it as a conservative preset cycle (`0 dB`, `-3 dB`,
  `-6 dB`) so limiter activity can be reduced without changing deck fader or
  crossfader semantics. The selected preset is persisted through
  `app_settings`/NVS and reapplied during P4 boot after `audio_engine_init()`;
- the post-sum master limiter uses a soft knee above roughly ±30000 PCM units:
  ordinary material below the knee is unchanged, while hot dual-deck sums are
  compressed toward the int16 ceiling instead of being hard-clipped. Limiter
  telemetry is accumulated in the audio mixer snapshot as cumulative limited
  sample counts, positive/negative overload counts, and peak pre-limit input.
  The P4 status indicator reports `CLIP n` only when the limited-sample counter
  increases, so normal transport status remains stable when no new limiting
  occurs;
- deck-local three-band EQ is applied in the wide P4 `audio_output_mixer` path
  after channel TRIM and before channel fader/crossfader summing. Raw FLX4 EQ
  values are kept in the mixer snapshot and exposed through `/api/status`;
  center is unity, minimum is band kill, and maximum is a conservative boost;
- Smart CFX and Smart Fader are P4-owned global states. Smart CFX enables the
  deck-local channel-filter DSP (a resonant ZDF state-variable filter with an
  exponential sweep, shaped by a smoothstep response curve) driven by the
  verified FLX4 filter knobs. Smart Fader
  keeps the physical crossfader authoritative but squares the fade-out side of
  the crossfader curve for a conservative transition assist. Both states drive
  FLX4 LEDs and are included in the mixer snapshot/status API;
- the audio engine exposes a central diagnostics snapshot with output codec
  state/sample-rate, late-output counters, per-deck ring fill and active flags,
  limiter counters, shared Beat FX Echo/Delay-line allocation/enabled/delay/mode
  state, and
  heap/internal/PSRAM free space. Internal, internal-DMA and PSRAM diagnostics
  additionally expose allocator lifetime minimum-free. Current largest-free
  blocks remain measured for internal/DMA only. The optional JSON
  `psram_largest_free` is null: IDF's full PSRAM TLSF walk masks interrupts and
  must not run in periodic status/esp_timer diagnostics while UAC sends audio
  or idle silence. `heap_walk_max_us` retains internal/DMA query wall-time
  maxima (including preemption; PSRAM entry zero, not measured).
  Heap queries run after releasing the audio state mutex, so
  these observation-time values do not extend the mixer critical section.
  `/api/status` includes these values under
  `diagnostics` so hardware smoke tests can read one structured report instead
  of scraping log lines;
- PCM5102A DMA uses the same 256-frame quantum as the audio output block.
  IDF's 240-frame default can make one write wait for two DMA completions.
  Descriptor count and the existing two-block late threshold are unchanged;
  physical sink latency must be measured on this geometry. Late-block journal
  details retain the actual block's dominant phase, separate from lifetime maxima;
- Beat FX state is P4-owned and read by both the physical Overview UI and
  `/api/status`. The effect selector uses the explicit cycle
  `FILTER → ECHO → FLANGER → DELAY → FILTER` (and the exact reverse for
  previous); `NONE=0` is a compatibility/sentinel enum value, not a selectable
  slot. CLEAR restores disabled FILTER defaults. DELAY is a full-band one-shot
  repeat whose Level/Depth controls wet gain, while ECHO remains a damped
  feedback effect with multiple repeats. Time is derived from effective BPM
  when Beat FX state is applied; it is not automatically retimed after later
  tempo, Beat Sync or track-load changes. Valid BPM is 40–300, with a 120 BPM
  fallback; time is capped at 1000 ms, and target BOTH currently derives one
  shared time from Deck 1 BPM.
  Both time effects share the existing per-deck stereo delay line, so DELAY
  adds no PSRAM allocation. The audio engine applies a square-root wet taper
  (maximum 0.70); Echo uses 0.20–0.68 feedback and Delay forces feedback to
  zero. Delay-time changes move the read head immediately, while switch-off
  leaves a bounded tail (~2 s for Echo, the previous period for Delay);
- ESP-Hosted Wi-Fi is enabled only when the Settings
  `wifi_remote` switch requests it; the HTTP server and captive DNS start after
  hosted Wi-Fi/AP init succeeds and are fully torn down when the switch is off;
- the shared output service relies on codec/I2S write pacing and does not add a
  second FreeRTOS delay after each output block;
- MP3 preload uses the bounded page cache for random-access reads while audio
  output is active, and MP3 seek table construction publishes the finished
  table with a short lock so loader/index work cannot hold the audio engine
  mutex for the full scan;
- FLAC cache callbacks publish a monotonic fault epoch and byte offset whenever
  a read ends early before the declared file end. FLAC open/read/seek therefore
  distinguish media faults from true EOF and replace/reseek the decoder at the
  last confirmed PCM frame without destroying the old decoder until recovery
  succeeds;
- Master Tempo is deck-local and P4-owned. The Overview `MT` buttons toggle a
  WSOLA-style overlap/correlation time-stretch reader over the canonical PCM
  timeline; scratch remains the higher-priority source, and ordinary resampling
  drains the final look-ahead tail near EOF;
- canonical PCM timeline cursors expose monotonic 64-bit sequences while the
  RV32 per-frame producer/consumer path retains 32-bit modular distances. Epoch
  changes use versioned snapshots, retained capacity is constrained below
  `2^31`, and scratch keeps a 64-bit origin across low-word wrap. Scratch
  release/re-grab control publishes only a packed command epoch; the output task
  alone mutates handoff gain and phase at block boundaries;
- stopping or reloading one deck must not close the codec while another deck is
  still loaded or playing;
- USB removal uses `audio_engine_suspend_loads_and_stop_all()` to close LOAD
  admission, tear down both decks and the shared output service, clear
  library/deck state, and only then calls `audio_engine_resume_loads()`.
- Library LOAD completion is allocation-free after task creation: its bounded
  result lives on the worker's fixed stack and is copied into the completion
  queue. Heap/PSRAM exhaustion therefore cannot bypass the LVGL completion that
  restores LOAD-button and status state.

Current P4 Overview waveform ownership rule:

- the Library/load path publishes deck-local waveform and beat-grid metadata,
  but it does not directly render the large main waveform;
- Overview owns the visual chrome around that state: compact D1/D2 badges, the
  title strip, BPM/pitch readouts, transport controls, deck VU meters, beat/phase
  strip, and effect-colour-coded Beat FX rail (Filter/Echo/Flanger/Delay, with a
  vertical depth meter). Those widgets render P4-owned deck, mixer, and Beat FX
  state; they do not become new state owners;
- the Overview scheduler owns main-waveform render/blit timing, including the
  shared Browse-rotate zoom window used by both deck panels;
- the large main waveforms use direct RGB565/PPA overlays for performance, so a
  track load arms a short reblit of both deck overlays to recover from LVGL
  flushes that can overwrite an already-rendered deck overlay;
- Beat Sync phase-align uses deck-core beat-grid state and preserves the
  reference deck's signed intra-beat offset before the Overview guide lines are
  redrawn.

## Data Flow

1. FLX4 sends a MIDI event, for example `0x90 0x0B 0x7F` for Deck 1 Play.
2. P4 USB1 decodes the USB-MIDI packet and `controller_runtime` maps it to a
   deck-aware semantic event.
3. `control_link_local` injects the event directly into the existing bounded
   deck queue; no UART framing occurs.
4. P4 updates authoritative state through `deck_core` and calls audio/mixer/UI
   APIs.
5. P4 builds the authoritative LED snapshot and `controller_led_runtime` sends
   USB-MIDI OUT directly through the USB1 owner.
6. On disconnect P4 releases held controls and clears the local profile; on
   reconnect it republishes connection state and the complete LED snapshot.

The control path distinguishes continuous values, physical held levels and
discrete commands. Continuous absolute values keep the latest sample and
relative motion accumulates deltas. Jog touch, Shift, Censor, Pad FX and shifted
roll use the P4-local desired/scheduled/dirty reconciler, so queue saturation
can delay but cannot erase their final level; disconnect forces releases and a
reconnect snapshot restores the physical state. Discrete commands remain FIFO
because collapsing repeated commands would change their meaning.

Connection level and non-VU controller LEDs follow the same convergence rule:
desired state remains dirty until the next layer accepts it. The P4 USB1 owner
replays both connected and disconnected levels periodically and retains an
already dequeued USB-MIDI OUT buffer across submit or retryable completion
failure. Controller-profile changes mark all known LED desired states dirty so
the new mapping receives a coherent refresh.

The MIDI map is not an authority for behavior. `docs/reference/Pioneer-DDJ-FLX4.midi.xml`
is the proven source for input status/midino values, and
`docs/reference/DDJ-FLX4_MIDI_message_List.md` is the additional official
reference for output LEDs and known XML/official-list conflicts. P4 behavior is
implemented explicitly in the owning P4 component.

Active `master` path frozen by the M2.4 production release:

- P4 USB0 remains the storage root and P4 USB1 directly owns the FLX4 MIDI and
  four-channel UAC interfaces; only a direct root child with VID:PID
  `2B73:0045` enables the FLX4-specific audio and shifted LED behavior.
- The engine writes stereo MAIN to channels 1/2 and ramped cue/headphone audio
  to channels 3/4. A stateful exact-rational resampler converts 48 kHz engine
  blocks to the 44.1 kHz endpoint. A 2048-frame ring uses bounded one-frame
  trim/duplicate correction around its middle band.
- Three primed isochronous transfers raise the host task to its active priority;
  disconnect/fault lowers it before halt/flush/recycle. MIDI queue entries carry
  a connection generation, so stale packets cannot cross a reconnect.
- There is no monitor-link fallback. Direct-UAC ring pressure and data loss are
  sampled outside the audio path and rate-limited into the service log.
- USB0 storage publishes desired connect/disconnect state from the MSC callback
  and reconciles it on one storage-owner task. Teardown first detaches the VFS
  mount and MSC handle from shared state, retires the fixed 8 KiB transfer, then
  destroys callback-backed resources. Larger FatFs operations are split into
  bounded 8 KiB SCSI transactions; no transfer object is replaced during I/O.
- Root recovery is indexed. A root is powered off only if the HCD still reports
  it disconnected with no pending event; an active attach or enumeration
  suppresses recovery. The completed lifecycle matrix covers both insertion
  orders, USB0/USB1 removal, active load/decode removal and reboot recovery.
- USB1 controller transfer and UAC faults share one bounded fault epoch. The
  owner first stops MIDI OUT/UAC acceptance, retires active endpoint callbacks
  and releases device/interface ownership, then submits at most one deferred
  root-recovery request. A physical device-gone event cancels that soft request.
  The P4 matrix records the earlier reconnect and non-OTA post-reboot
  dual-playback evidence. M2.4 does not inherit its signed-OTA recovery result:
  USB0 and USB1 remained unenumerated after the M2.4 OTA software reboot until
  a cold power cycle. See
  [P4_DUAL_USB_LIFECYCLE_MATRIX_20260911.md](validation/P4_DUAL_USB_LIFECYCLE_MATRIX_20260911.md)
  and
  [M2_4_PRODUCTION_RELEASE_20260929.md](validation/M2_4_PRODUCTION_RELEASE_20260929.md).

The retired S3 UART and monitor-I2S implementation is available only in Git
history.

## Main Code Surfaces

- `firmware/main-deck-p4/components/usb_host_manager/` — shared Host Library
  and per-root recovery arbitration.
- `firmware/main-deck-p4/components/usb_storage/` — USB0 MSC/media lifecycle.
- `firmware/main-deck-p4/components/controller_usb_host/` — USB1 composite
  MIDI/UAC ownership.
- `firmware/main-deck-p4/components/p4_local_controller/` — connection,
  profile, semantic dispatch and LED integration.
- `firmware/main-deck-p4/components/control_link/control_link_local.c` — narrow
  compatibility adapter into the existing semantic event queue.
- `firmware/main-deck-p4/components/controller_runtime/` and
  `controller_led_runtime/` — MIDI mapping and direct feedback.
- `firmware/main-deck-p4/components/audio_engine/`, `deck_core/` and `ui/` —
  authoritative behavior and presentation.

Current P4 mixer/audio surfaces live in `audio_engine` helpers such as
`audio_output_mixer`, deck-local runtime/preload/task-context modules, and the
shared output service. A separate `mixer/` component is not currently required.

## State Ownership

The most important architectural rule is simple: MIDI is an input transport, not
state. The FLX4 mapping file tells us what the controller sends and accepts; it
does not define the playback model.

`deck_core` and the audio engine on P4 own the actual state.

## Wi-Fi Remote

The embedded Wi-Fi Remote lives in
`firmware/main-deck-p4/components/web_server/web/` and is served directly from
the P4 firmware image. It is an operator client, not an alternate state owner:

- `/api/status` publishes authoritative deck, SYNC and mixer snapshots;
- marked POST requests translate remote actions into the same semantic
  `deck_core` events used by the physical controller;
- slider traffic is bounded and waveform dragging stays local until one final
  seek is committed;
- library loads retain media-generation identity, and OTA/profile maintenance
  retains its existing signature, validation and confirmation boundaries;
- all layout assets are embedded locally so the SoftAP path has no internet
  dependency.

The browser contract suite in `tests/web_ui_contract/` checks the embedded asset
boundary, handler wiring, failed-mutation behavior and single-shot seek rule.

## Data-Driven Multi-Controller Platform

The fork-improvements branch accepts S3CP v4 alongside unchanged v2/v3.
Initial SysEx, press-only selectors, output scale and channel filter policy
are declared in the profile. USB generation and connection epoch bind worker
initialization; LED/MIDI mapping waits for initialization enqueue completion.
Storage replacement queues worker reactivation. See
[package D evidence](validation/FORK_IMPROVEMENTS_PACKAGE_D_SOFTWARE_20261004.md).

The P4-local runtime supports controllers other than the DDJ-FLX4 **without a
firmware rebuild**, using data-driven controller profiles. The FLX4 remains the
first supported controller and its built-in C map stays as a fallback. Format
details: `docs/CONTROLLER_PROFILE_SCHEMA.md`.

Roles:

- **Windows Profile Builder** (planned, out of firmware scope): scans a
  controller, runs MIDI/LED learn wizards, and exports `profile.json` +
  compiled `profile.s3bin`.
- **SD/TF card**: holds `/controllers/<name>/profile.s3bin` (one directory per
  controller). Rekordbox media stays on the USB drive; profiles live on the SD.
- **P4 `controller_profile_manager`**: scans `/sd/controllers` at boot, validates
  each S3CP header (magic/version/CRC), keeps a registry, matches the connected
  controller by VID/PID and activates the profile locally. It also serializes
  atomic web installs/rescan against activation.
- **P4 `controller_profile_runtime`**: holds the active profile and runs the
  table-driven MIDI-in and LED-out maps using the same semantic vocabulary as
  the built-in FLX4 map.

The repository also carries a host-qualified
`hercules_djcontrol_inpulse_500` profile. It exercises a real non-FLX4 layout,
controller-specific RGB pad values, the relative `deckN.loop_size` semantic and
the idempotent `sync_off` extended action. This is software evidence only: the
physical Inpulse 500 descriptor, MIDI/LED reconnect path and four-channel USB
audio routing remain hardware gates.

Flow (adds to the base data flow above):

```text
controller connect
  -> P4 USB1 owner publishes VID/PID/caps/product locally
  -> P4 matches and validates a profile in /sd/controllers
  -> P4 controller_profile_runtime activates it synchronously
  -> P4 maps MIDI IN and LED OUT locally (built-in FLX4 map fallback)
  -> P4 deck_core / audio_engine / UI are unchanged: they still receive the
     same semantic events and send the same semantic LED frames
```

Maintenance flow:

```text
compiled profile.s3bin
  -> P4 Wi-Fi Remote POST /api/controller-profile
  -> strict directory ID + bounded body + S3CP length/CRC validation
  -> same-directory upload, fsync, backup and atomic rename on SD
  -> locked registry rescan
  -> matching profile is activated locally for the connected controller
```

`/api/status.controller.active_profile` is empty until local validation and
activation succeed. During bring-up, `profile_state` retains the compatible
`matched`, `transferring`, `active`, `failed`, or `unsupported` vocabulary.

Design guarantees:

- P4 stays the sole authority for deck/audio/UI/mixer state; the profile only
  changes how raw controller MIDI is translated to and from the semantic bus.
- The compiled FLX4 profile is proven byte-equivalent to the built-in
  `flx4_map`/`flx4_led_midi` by a golden-parity host test (12k-message input
  sweep + snapshot + 690-combo LED parity), so routing FLX4 through the dynamic
  profile reproduces the built-in behaviour exactly.
- Every committed fixture, including Hercules, is deterministically generated
  by the profile tools. Hercules also has explicit runtime, registry, RGB and
  P4 behavior assertions; it is not advertised as hardware-
  supported until the checklist in its mapping document passes.

Verified on hardware 2026-07-09: the SD profile loads into the P4 registry and
`/api/status` reports `profiles:1`.

Active P4 components and retained profile tooling:

```text
firmware/main-deck-p4/components/
  controller_profile/          S3CP parser + table-driven MIDI/LED matcher (pure C)
  controller_profile_runtime/  active-profile holder + dynamic P4 mapper
  controller_profile_manager/  SD scan, registry, VID/PID match, local activation
  control_link/                 local semantic queue + direct LED snapshot sink
tools/controller_profile/
  compile_profile.py           profile.json -> profile.s3bin compiler
controllers/pioneer_ddj_flx4/  hand-written FLX4 profile.json + compiled .s3bin
```

The former S3-side runtime and `0xA6` transfer codec remain only in Git history;
neither is part of the active P4 image.
