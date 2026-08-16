# Architecture

Status: current architecture, audited 2026-07-30. P4 remains authoritative;
S3 remains a transport/translation and USB-audio bridge. The ESP-IDF 6.0.2
migration is merged into `master` (release line `RC2`); it added the bounded
compressed audio cache, paginated Library UI and recorder safety hardening
without changing the P4/S3 ownership split.

## High-Level Flow

```text
Pioneer DDJ-FLX4
    |
    | USB MIDI
    v
ESP32-S3 control-board-s3
    |
    | 0xA5 UART semantic events
    v
ESP32-P4 main-deck-p4
    |
    | dual decode + mixer
    v
Master output + cue/PFL output
```

## ESP32-S3 Responsibilities

The S3 firmware should be renamed conceptually from panel controller to FLX4
controller, but the directory can stay `control-board-s3` until code movement
is justified.

Responsibilities:

- enumerate the DDJ-FLX4 through ESP-IDF USB host;
- parse class-compliant USB MIDI packets;
- translate MIDI status/midino/value triples into semantic events;
- combine MSB/LSB pairs for 14-bit controls before forwarding when practical;
- coalesce high-rate jog and analog values locally so stale motion does not
  flood the UART queue;
- publish durable DDJ-FLX4 USB connection/disconnection desired state to the
  P4, including periodic replay of both levels after a lost UART frame;
- send heartbeat frames to the P4;
- receive P4 LED/state frames;
- emit FLX4 MIDI LED feedback using XML/official-list output addresses; non-VU
  LED state is retained until enqueue/USB completion can converge, while VU is
  intentionally best-effort;
- host the runtime-only S3 service AP and S3 OTA endpoint when requested by P4;
- stream P4 monitor PCM to the FLX4 USB Audio headphone endpoint.

The S3 must not:

- decide whether a deck is playing;
- calculate audio position;
- apply mixer gains itself;
- invent current/next track state.

## ESP32-P4 Responsibilities

The P4 remains authoritative for performance state.

Responsibilities:

- load Rekordbox tracks and analysis data from USB media;
- own two `deck_core` state instances;
- own the mixer state: channel faders, crossfader, pregain, EQ/filter when
  implemented, cue/PFL selection;
- own controller behavior that changes playback state, including Hot Cue,
  Loop, Beat Jump, Tempo Range, and the current one-shot Beat Sync signed
  intra-beat phase-align behavior;
- decode audio and write master/cue buffers to hardware;
- render UI state;
- send LED feedback commands to the S3;
- force a P4-owned LED snapshot after an FLX4 reconnect so physical LEDs
  recover without S3 owning playback state.

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
2. S3 MIDI host parses it and maps it to a deck-aware event.
3. S3 sends a `control_link` UART frame to P4.
4. P4 updates the target deck state through `deck_core`.
5. P4 calls audio engine/mixer APIs.
6. P4 sends LED feedback back over `control_link`.
7. S3 emits the matching MIDI LED message to the FLX4.
8. If the FLX4 disconnects/reconnects, S3 publishes connection state and P4
   republishes the current P4-owned LED snapshot.

All S3 `0xA5` and `0xA6` transmitters share one serializer. Sequence
allocation, complete frame construction and the UART write occur under the same
static mutex, so concurrent heartbeat, semantic, descriptor and profile replies
cannot appear on wire out of sequence. A failed write still consumes its
sequence and is therefore visible to P4 gap telemetry.

The control path distinguishes continuous values, physical held levels and
discrete commands. Continuous absolute values keep the latest sample and
relative motion accumulates deltas. Jog touch, Shift, Censor, Pad FX and shifted
roll use a shared S3/P4 desired/scheduled/dirty reconciler, so queue saturation
can delay but cannot erase their final level; disconnect forces releases and a
P4 reboot reaccepts the S3 snapshot. Discrete commands remain FIFO and retain
sequence-gap telemetry because collapsing repeated commands would change their
meaning.

Connection level and non-VU controller LEDs follow the same convergence rule:
desired state remains dirty until the next layer accepts it. The S3 USB owner
replays both connected and disconnected levels periodically and retains an
already dequeued USB-MIDI OUT buffer across submit or retryable completion
failure. Controller-profile changes mark all known LED desired states dirty so
the new mapping receives a coherent refresh.

The MIDI map is not an authority for behavior. `docs/reference/Pioneer-DDJ-FLX4.midi.xml`
is the proven source for input status/midino values, and
`docs/reference/DDJ-FLX4_MIDI_message_List.md` is the additional official
reference for output LEDs and known XML/official-list conflicts. P4 behavior is
implemented explicitly in the owning P4 component.

Current S3 firmware modes:

- DDJ-FLX4 product mode: USB MIDI host for translator input,
  connection-state publication, and MIDI LED output;
- optional raw logger: disable `CONFIG_DDJ_FLX4_TRANSLATE_TO_P4` to retain
  descriptor/MIDI capture without forwarding events to P4.

R5D permanently retired the inherited CDJ GPIO panel/TinyUSB-device mode.
The S3 USB OTG peripheral is now unconditionally a host; `panel_io`,
`midi_compat`, `calibration` and `CONFIG_DDJ_FLX4_HOST_MODE` no longer exist.

FLX4 USB headphones path (**hardware-validated 2026-07-02; XIAO wiring
validated 2026-07-06; S3 overrun regression fixed and re-smoked 2026-07-09**,
`docs/validation/FLX4_USB_AUDIO_E2E_SMOKE.md`):

- P4 owns the monitor/cue mix and publishes stereo 16-bit `hp_out` blocks
  through `monitor_pcm_link` (I2S TX master, unit 0).
- The dedicated P4-to-S3 monitor PCM payload uses `P4HP` blocks (sequence
  numbers + CRC32 over protected header plus payload) over I2S, intentionally
  separate from the `0xA5` UART control protocol. Current XIAO wiring pins:
  P4 GPIO32/GPIO34/GPIO35 -> S3 GPIO7/GPIO8/GPIO9, 64 kHz stereo slots.
- S3 `p4_audio_link` (I2S slave RX) deframes into a 4096-frame ring; the
  `flx4_usb_audio` UAC streamer drains the ring into isochronous OUT transfers,
  mapping P4 `hp_out` onto the FLX4 4-channel format's headphone pair
  (channels 3/4). Ring streaming autostarts once ~20 ms is buffered and matches
  the FLX4 endpoint rate to the P4 output rate (44.1 / 48 kHz). While already in
  ring-streaming mode, S3 continues to track `p4_audio_link.sample_rate` and
  reinitializes the USB packetizer if the P4 link rate changes; otherwise the
  producer and USB consumer drift apart and the 4096-frame ring can overrun.
- Output topology (P4 has 2 usable I2S units; unit 2 freezes on eco2):
  **PCM5102A RCA = MAIN OUT (unit 1, paces the loop)**, **FLX4 USB = CUE/MONITOR
  (link on unit 0)**, **ES8311 onboard monitor disabled** to free unit 0. Both
  outputs run simultaneously. S3 stays the FLX4 USB host and keeps MIDI
  responsive while streaming audio.

## Main Code Surfaces

Inherited files that will be touched early:

- `firmware/control-board-s3/main/app_main.c`
- `firmware/control-board-s3/components/control_link/`
- `firmware/control-board-s3/components/flx4_midi_host/`
- `firmware/main-deck-p4/components/control_link/`
- `firmware/main-deck-p4/components/deck_core/`
- `firmware/main-deck-p4/components/audio_engine/`
- `firmware/main-deck-p4/components/ui/`

Current S3 FLX4 component:

```text
firmware/control-board-s3/components/flx4_midi_host/
  include/flx4_midi_host.h
  include/flx4_map.h
  flx4_midi_host.c
  flx4_map.c
```

Current P4 mixer/audio surfaces live in `audio_engine` helpers such as
`audio_output_mixer`, deck-local runtime/preload/task-context modules, and the
shared output service. A separate `mixer/` component is not currently required.

## State Ownership

The most important architectural rule is simple: MIDI is an input transport, not
state. The FLX4 mapping file tells us what the controller sends and accepts; it
does not define the playback model.

`deck_core` and the audio engine on P4 own the actual state.

## Data-Driven Multi-Controller Platform

The S3↔P4 split also enables supporting controllers other than the DDJ-FLX4
**without a firmware rebuild**, using data-driven controller profiles. The
FLX4 remains the first supported controller and its built-in C map stays as a
fallback; the platform makes it one profile among many rather than the only
model. Format details: `docs/CONTROLLER_PROFILE_SCHEMA.md`.

Roles:

- **Windows Profile Builder** (planned, out of firmware scope): scans a
  controller, runs MIDI/LED learn wizards, and exports `profile.json` +
  compiled `profile.s3bin`.
- **SD/TF card**: holds `/controllers/<name>/profile.s3bin` (one directory per
  controller). Rekordbox media stays on the USB drive; profiles live on the SD.
- **P4 `controller_profile_manager`**: scans `/sd/controllers` at boot, validates
  each S3CP header (magic/version/CRC), keeps a registry, and matches the
  connected controller by VID/PID. On a match it streams the `.s3bin` to the S3
  and reports connection/profile state through `/api/status`. The same manager
  serializes web installs against profile transfer, atomically swaps the SD
  file, rescans into a new registry snapshot, invalidates the old activation
  cache and queues the matching profile for S3 activation again.
- **S3 `controller_profile` + `controller_profile_runtime`**: parse the received
  profile and run a table-driven MIDI-in mapper and LED-out mapper that emit the
  same `control_link` semantic vocabulary the built-in map uses.

Flow (adds to the base data flow above):

```text
controller connect
  -> S3 sends CONTROLLER_DESCRIPTOR (VID/PID/caps/product) over 0xA6 bulk frame
  -> P4 matches a profile in /sd/controllers and streams it back (0xA6 transfer)
  -> S3 verifies crc32, ACKs, activates
  -> S3 maps MIDI IN and LED OUT through the active profile (FLX4 map fallback)
  -> P4 deck_core / audio_engine / UI are unchanged: they still receive the
     same semantic events and send the same semantic LED frames
```

Maintenance flow:

```text
compiled profile.s3bin
  -> P4 Wi-Fi Remote POST /api/controller-profile
  -> strict directory ID + bounded body + S3CP length/CRC validation
  -> same-directory upload, fsync, backup and atomic rename on SD
  -> locked registry rescan + old S3 activation cache invalidation
  -> profile sender transfers and activates the matching profile on S3
```

`/api/status.controller.active_profile` is deliberately stricter than a VID/PID
match: it is empty until the P4 sender receives the S3 ACK for
`PROFILE_ACTIVATE`. During bring-up, `profile_state` reports `matched`,
`transferring`, `active`, `failed`, or `unsupported`.

Design guarantees:

- P4 stays the sole authority for deck/audio/UI/mixer state; the profile only
  changes how raw controller MIDI is translated to and from the semantic bus.
- The 0xA6 frame codec and the transfer receiver are byte-identical on both
  sides (asserted by host tests) so the link cannot disagree on the wire.
- The compiled FLX4 profile is proven byte-equivalent to the built-in
  `flx4_map`/`flx4_led_midi` by a golden-parity host test (12k-message input
  sweep + snapshot + 690-combo LED parity), so routing FLX4 through the dynamic
  profile reproduces the built-in behaviour exactly.

Protocol details: `docs/CONTROL_LINK_PROTOCOL.md` (0xA6 Bulk Frame Layer).
Verified on hardware 2026-07-09: the SD profile loads into the P4 registry and
`/api/status` reports `profiles:1`.

New components:

```text
firmware/control-board-s3/components/
  controller_profile/          S3CP parser + table-driven MIDI/LED matcher (pure C)
  controller_profile_runtime/  active-profile holder + dynamic mapper (S3)
firmware/main-deck-p4/components/
  controller_profile_manager/  SD scan, registry, VID/PID match, profile sender
firmware/*/components/control_link/
  ctrl_bulk.c                  0xA6 frame codec (byte-identical both sides)
  cp_xfer.c                    profile-transfer receiver + crc32 (byte-identical)
tools/controller_profile/
  compile_profile.py           profile.json -> profile.s3bin compiler
controllers/pioneer_ddj_flx4/  hand-written FLX4 profile.json + compiled .s3bin
```
