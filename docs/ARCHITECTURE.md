# Architecture

Status: P4-only branch architecture, updated 2026-08-27. The P4 is both the
authoritative playback/UI engine and the direct dual-root USB host. The former
S3 transport/audio bridge and firmware target have been removed. Their dated
design and validation records remain historical evidence only.

## High-Level Flow

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
  starting playback; the ES8311 monitor path and PCM5102A main path must stay
  sample-rate aligned;
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
  heap/internal/PSRAM free space. `/api/status` includes these values under
  `diagnostics` so hardware smoke tests can read one structured report instead
  of scraping log lines;
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

Active `feat/p4-dual-usb-host` path (software-qualified 2026-08-27):

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

The prior S3 UART and monitor-I2S implementation remains available only in Git
history and dated validation/protocol records.

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

## Data-Driven Multi-Controller Platform

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

The former S3-side runtime and `0xA6` transfer codec remain in the historical
S3 source tree and Git history; neither is part of the active P4 image.
