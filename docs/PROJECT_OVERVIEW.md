# Pajoniiir Project Overview

Status: P4-only branch overview, reconciled 2026-08-27. The ESP-IDF 6.0.2
migration is **merged into `master`**; the release prefix moved from `RC1` to
**`RC2`** to mark the new build baseline, and the latest clean dual-target
release build is `RC2` (`56905c89`, ESP-IDF 6.0.2). It carries the bounded
compressed audio cache, paginated Library UI, immutable track sort, recorder
safety hardening and the full `fix/release-blockers-and-concurrency`
stabilisation set.

The last fully recorded dual-processor bench ran the ESP-IDF v6.0.2 RC2 line: P4 was
`RC2-3-g136aad7` on factory and S3 is exact `RC2` on valid `ota_0`. Both RC2
applications were first installed successfully through OTA. Complete wired
boot-chain flashes then passed, the P4 SDMMC ownership regression was fixed,
and a focused smoke passed display/touch/Library, FLX4 MIDI/LED,
PCM5102A MAIN, FLX4 CUE/MONITOR and real-MP3 playback. Real WAV/FLAC cache
testing did not run because the audited Rekordbox USB contained stale catalog
rows but no physical WAV/FLAC files. The latest fully functionally accepted
complete-system baseline therefore remains `RC1-123-g587cd7a1`; targeted
Phase 20/E1A, remaining IDF6 recovery/sustained-load rows, remote-profile
update, enclosure endurance and production hardening remain open.

## Goal

Pajoniiir is a standalone two-deck DJ player using the Pioneer DDJ-FLX4 as the
operator surface. It does not run Mixxx and it does not require a PC during
performance. The DDJ-FLX4 supplies controls and LEDs; the ESP32-P4 provides
media browsing, playback, audio output, and display.

## Product Shape

On `feat/p4-dual-usb-host`, the active product has one firmware target:
`firmware/main-deck-p4`. USB0 owns Rekordbox storage and USB1 owns the direct
DDJ-FLX4 MIDI, LED and four-channel USB Audio path. Controller profiles are
loaded and activated locally on the P4.

The P4 owns all authoritative deck state. It loads Rekordbox media from USB,
tracks current position, controls audio decode, drives the local display,
decides which LEDs should be on and writes cue audio directly to the FLX4.
`firmware/control-board-s3` remains in the repository as historical/reference
source, but is not built, packaged or required by the active product.

## Inherited Firmware Baseline

The imported baseline code already provides:

- ESP32-P4 JC4880 board support for ST7701S display, GT911 touch, ES8311 audio,
  USB mass storage, and SD card cache/config.
- Rekordbox `export.pdb` and `ANLZ0000.DAT` parsing.
- A single-deck `deck_core` state machine.
- A single-track `audio_engine` with MP3 preload, minimp3 decode, pitch
  resampling, instant seek, hot cue, beat jump, and loop support.
- An ESP32-S3 firmware target with UART `control_link`.
- PC unit tests for parsers, audio engine, control link, and selected UI logic.

## Capability Ledger

This section retains dated acceptance evidence. S3-specific bullets describe
the historical `master` path; the active branch replacement is the direct P4
USB1 path documented explicitly below.

- P4 USB1 descriptor parsing is hardened, and the local controller runtime maps
  FLX4 MIDI messages directly into deck-aware semantic events. No UART frame or
  feature overlay is required.
- Physical DDJ-FLX4 enumeration and raw packet capture were completed on
  2026-06-14. All MVP controls matched the vendored Mixxx XML mapping, and the
  translator is enabled by default.
- Remaining extended controls use XML status/midino and encoding as
  implementation seeds. Mixxx script callbacks are not runtime logic; the P4 remains
  authoritative for standalone behavior and state.
- The official Pioneer MIDI message list is vendored beside the Mixxx XML and
  is used as the secondary reference for LED outputs and documented conflicts;
  current implementation still treats the XML as the proven input source.
- The DDJ control-link namespace is deck-aware, and the P4 parser carries deck
  and control fields for DDJ events while preserving legacy frames.
- P4 `deck_core` now stores independent Deck 1/Deck 2 state and routes local
  UI operations through deck-aware APIs.
- P4 audio has per-deck engine/ring/resampler/preload/runtime/task context
  storage, a shared output mixer, channel fader/crossfader gain handling,
  deck-local three-band EQ, and Deck 2 producer support. Compressed audio uses
  a bounded LRU page cache (8 × 32 KiB per deck) instead of loading the entire
  file into contiguous PSRAM; this eliminates `TRACK TOO LARGE` errors and
  PSRAM fragmentation under large files. Stereo Master and Split Mono cue/PFL
  routing are implemented for the current output path.
- P4 LVGL UI is dual-deck: Overview, Library load paths (paginated 8-row
  table with PREV/NEXT navigation), performance target selection, Settings,
  status/header, and waveform rendering are split into smaller UI modules.
- Both deck Overview waveforms use the direct PPA overlay path (the 2026-06-13
  Deck 2 jitter that once forced an LVGL path is resolved). The waveform renders
  with the "Punchy" colour scheme (bright cyan transients, white transient tips),
  an active/armed loop-region amber highlight with edge markers, hot-cue markers
  on the large + mini waveforms, and a translucent played-progress overlay on the
  mini — all baked into the scrolling RGB565 strip so they PPA-blit without
  LVGL-over-PPA flicker.
- ESP-Hosted Wi-Fi is re-enabled behind a Settings switch (default off): the
  onboard ESP32-C6 provides a WPA2 SoftAP (`Pajoniiir`) and P4 serves a mobile web
  controller at `http://192.168.4.1` (deck status, library load, transport,
  mixer, seek). A 2026-07-04 audit added RELAXED atomics for shared audio/mixer
  state, clean load-failure abort, and dynamically-sized web status JSON, and a
  USB-disconnect crash (ungated track-meta-cache `stat()`) was fixed.
- The current Overview UI uses Pioneered-style deck strips, compact D1/D2 deck
  badges, a separated transport/VU lane, centered beat/phase indicators, a
  compact blue title strip with remaining-time pill, readable BPM/pitch
  indicators, an effect-colour-coded Beat FX rail with a vertical depth meter, and bounded title/timer invalidation so
  status chrome does not create continuous redraw pressure.
- Overview waveform loading and zoom are stabilized for the current dual-deck
  path: Browse rotate controls a shared 4/8/12/16/24-beat main-waveform zoom
  while Overview is active, track load defers main waveform rendering to the
  Overview scheduler, and both deck overlays are briefly reblitted after any
  load so one deck's direct overlay does not disappear when the other deck is
  loaded.
- The 2026-07-17 P4 display pass phase-locks the firmware UI update to delivered
  DSI refresh events and runs the panel at 49.981 Hz by extending vertical front
  porch. Both playing deck waveforms retain full per-frame scheduling without
  the one-deck alternation that looked "underwater". The original 132-second
  development smoke and a subsequent more-than-71-second exact-candidate COM15
  re-smoke both ended with zero DSI underruns and zero monitor-PCM drops; the
  operator confirmed fluid motion with no visible flash or jitter. See the
  [focused validation record](validation/P4_OVERVIEW_DSI_SYNC_SMOKE_20260717.md).
- FLX4 Play/Cue/PFL LED MIDI output is implemented through P4-owned snapshots
  and the direct USB1 MIDI Out queue. P4 also drives selected
  pad-mode LEDs, Beat Sync state LEDs, Loop In/Out LEDs from pending marker and
  active loop state, normal Hot Cue pad LEDs, normal Beat Loop pad LEDs, and VU
  meter output.
- P4 USB1 publishes DDJ-FLX4 connection state locally and forces a P4-owned LED
  snapshot on reconnect. Historical verification on 2026-06-20 confirmed
  Play/Cue/PFL LED recovery without playback or deck-state changes; extended
  pad-mode/sync/loop reconnect behavior on the former S3 path; equivalent
  direct-P4 physical reconnect/reset acceptance remains open.
- Smart CFX and Smart Fader raw inputs are captured and mapped as momentary
  semantic button events. P4 now owns their state, LED feedback, status
  exposure, Smart CFX filter DSP with a smoothstep response curve (fine near
  the detent, ~1:1 at half turn), and Smart Fader transition-assist behavior.
- Settings UI polish removed out-of-scope Key Shift presentation, removed the
  retired monitor-speaker switch from the active UI, darkened wireless switch
  off states, and collapsed the lower mixer/PFL routing block into a compact
  status strip.
- Hot Cue pad behavior is implemented on P4: an empty pad stores the current
  per-track deck position, an occupied pad recalls it with audio seek, and
  Shift + pad clears the slot. Deck 1 hardware behavior smoke passed on
  2026-06-21; Deck 2 behavior uses the same deck-local path and remains marked
  for hardware smoke.
- Phase 7 extended controller work is merged into `master` as of 2026-06-26.
  Implemented P4 behavior now includes Loop In/Out, Reloop/Exit, loop
  halve/double, Beat Jump buttons/pads, normal and shifted Beat Loop pads,
  Tempo Range, Beat Sync BPM-match-on-press with one-shot phase align while
  playing, and Hot Cue store/recall/clear. Beat Sync preserves the reference
  deck's signed intra-beat offset so the Overview beat-match guide lines align
  after the one-shot seek; continuous beat following remains out of scope. Pad
  FX now has a first P4-owned DSP slice behind
  PAD_FX1/PAD_FX2 `CTRL_PAD_ACTION` events, physical FLX4 Pad FX pad input
  mapping from the official MIDI message PDF, Echo release-tail behavior, and
  host-tested momentary Pad FX pad LED feedback. Pad FX behavior and normal pad
  LED hardware smoke passed on 2026-07-01.
- The DDJ-FLX4 headphone jack receives the P4 cue/monitor mix directly over
  USB1 Audio Class while PCM5102A remains RCA MAIN. The former P4-to-S3
  monitor-I2S bridge is retired. Direct-path audio quality, reconnect and soak
  acceptance remain open; the 2026-07-02/09 S3 results remain reference
  behavior only.
- The Beat FX audio DSP was overhauled on 2026-07-10 for better sound and less
  touchy knob response: the one-knob channel Filter became a resonant ZDF
  state-variable filter with an exponential low-pass/high-pass sweep to full
  kill, the Echo gained per-generation feedback damping and a ring-out tail,
  Smart CFX moved to a smoothstep response curve, and a beat-derived **Flanger**
  was added as a third Beat FX effect (cycle FILTER → ECHO → FLANGER). The
  Overview Beat FX rail was redesigned to be effect-colour-coded
  (Filter blue / Echo amber / Flanger magenta) with a vertical depth meter.
- On 2026-07-16, **Delay** was added as Beat FX value `4` without renumbering
  the existing effects. It is a full-band one-shot repeat without feedback;
  Level/Depth controls its wet gain. Its time is derived from effective BPM
  when Beat FX state is applied, not continuously resynchronized after later
  tempo, Beat Sync or track-load changes. Delay and Echo reuse the
  same per-deck stereo delay line, so the new mode does not allocate additional
  PSRAM, while Echo keeps its damped multi-repeat feedback behavior. Time is
  calculated from 40–300 BPM with a 120 BPM fallback, capped at 1000 ms, and
  target BOTH currently uses Deck 1 BPM. The
  selector now cycles `FILTER → ECHO → FLANGER → DELAY → FILTER`, with the
  reverse order on Previous; `NONE=0` is a non-selectable compatibility
  sentinel and CLEAR restores disabled FILTER defaults. Software acceptance is
  covered by the P4 host suites and `idf.py build`; physical FLANGER and DELAY
  sound/target/beat/depth smoke remains pending.
- The P4 default build now contains PCM5102A MAIN plus direct FLX4 USB
  headphones; no feature overlay or second-board audio build is required.
- The seven official DDJ-FLX4 MIDI gaps were closed against the official Pioneer
  MIDI message list: Censor, Sync (set-master), Quantize, Loop Adjust In/Out,
  Reloop+Stop, Headphone Level, and the shifted Browse/Load/Beat-FX controls.
  The P4-local mapper maps the physical MIDI notes directly to semantic events
  and P4 owns the resulting state/audio/LED behavior. `master` retains the earlier
  seek-back Censor approximation; `feat/p4-dual-usb-host` replaces it with a
  gapless slip-reverse reader over retained canonical PCM and a 10 ms release
  crossfade, without moving the normal playhead.
  Merged to `master` on 2026-07-02; host suites pass. The XIAO RC2 smoke on
  2026-07-07 confirmed the connected FLX4 headphone/mixer control path with
  audible USB headphone output; remaining per-control exceptions stay tracked in
  `docs/DDJ_FLX4_MIDI_MAP.md`.
- The active direct-P4 FLX4 path now owns USB-MIDI and four-channel UAC
  on USB1 while USB0 remains the Rekordbox storage root. It applies a
  fail-closed HS/FS FIFO source patch, supports 44.1/48 kHz engine input,
  resamples to the FLX4 44.1 kHz endpoint, sends MAIN on channels 1/2 and cue on
  3/4 with no S3 fallback. Host-side health tracking reports bounded ring pressure and data
  loss; physical audio quality, reconnect and long-soak acceptance remain open.
- The FLX4 USB headphones and official-MIDI-gap-closure feature branches were
  merged to `master` and deleted after the 2026-07-02 work. On 2026-07-03 the
  remaining stale Codex branches were reviewed and removed (local + remote),
  including the old experimental `codex/flx4-extended-controls`, whose verified
  slices had already been salvaged into `master`. That is a historical cleanup
  record, not a claim about the repository's current branch inventory. The
  2026-07-20 audit reduced the inventory to a single `master` branch: all merged
  `codex/*` and `feature/*` branches were pruned local + remote, and the last
  branch `codex/phase-8-implementation` was archived under tag
  `attic/phase-8-status-led-policy` before removal. A follow-up audit on
  2026-07-26 found five new remote maintenance branches and two stale local
  tracking branches; every tip was already an ancestor of `master` with 0
  unique commits, so all seven were removed. Only `master` remains locally and
  on `origin`. The canonical repository is
  `https://github.com/dvucinozd/Pajoniiir.git`.
- Vinyl/scratch is hardware-validated on both decks: platter touch selects a
  canonical PSRAM PCM timeline for forward/reverse scratch, including paused
  and CUE states, active-loop wrapping, clean window edges and click-free
  release/re-grab.
- Master Tempo/key lock is implemented in the P4 audio callback and exposed by
  the Overview `MT` control. Basic pitch/key behavior passed hardware smoke;
  longer simultaneous-deck quality and CPU tuning remains an optimization item.
- Library sorting operates on immutable track records with a compact
  double-buffered `uint16_t` row-order array. Full-record copies and qsort
  over large structs are eliminated.
- The master-output recorder is shelved and compiled out by default, but its
  safety was hardened: STOP closes the producer gate and waits for in-flight
  producers before drain; finalise uses a transactional
  `patch` → `sync` → `close` → `publish` pipeline that propagates every
  durability failure and refuses to rename a partial `.part` file.
- P4 OTA uses alternating slots, validates target/chip/project, confirms health
  after mandatory startup, preserves the active image on interrupted upload and
  rolls back an unconfirmed
  image. Full signed-path acceptance used `RC1-123-g587cd7a1` on 2026-07-14;
  S3 results in that record are retained as historical dual-board evidence;
  current release packages contain P4 only.

## Non-Goals For The First Milestone

- Full Mixxx feature parity.
- Continuous beat following or full Rekordbox-style sync state. Master Tempo
  key lock is implemented; advanced phase-vocoder quality work remains outside
  the first milestone.
- Deeper Beat FX and Pad FX hardware acceptance beyond the current Smart CFX
  smoothstep curve, Smart Fader transition-assist V1 behavior, the Beat FX
  recorded Filter/Echo hardware baseline, the software-covered Flanger/Delay
  modes, and the host-tested Pad FX DSP/input/LED slice.
- Four-deck support.
- Rekordbox library editing.
- Running JavaScript Mixxx mappings on-device.
- Treating FLX4 MIDI XML as executable logic.

## Implemented Follow-Up And Remaining Acceptance

Two design plans have since been implemented in firmware (details in Phase 8 of
[DEVELOPMENT_PLAN.md](DEVELOPMENT_PLAN.md)):

- **Historical S3 XIAO GPIO21 user status LED** — retained as the S3 `status_led`
  component for FLX4 host modes. It uses the active-low XIAO onboard user LED
  for reduced USB/link/activity feedback; it does not encode playback state.
- **WAV + FLAC playback** — implemented through the decoder-abstraction layer
  and bounded firmware cache path. MP3 keeps PVBR seek support; WAV/FLAC use
  decoder metadata while Rekordbox/ANLZ still supplies beatgrid/BPM/waveform
  context. WAV is currently limited to classic RIFF/WAVE PCM16 mono/stereo.
- **Wi-Fi web UI mobile controller** — implemented (ESP-Hosted SoftAP, Settings
  toggle, captive portal). Remaining web-UI scope (EQ/filter/hot-cue/beat-jump
  controls, waveform) is intentionally deferred — the web UI stays a simple
  remote, not a full control surface.
- **Overview waveform visualisations** — implemented ("Punchy" colours, loop
  highlight, hot-cue markers, mini played-progress).

The clean P4 waveform candidate, signed paired package and exact-image focused
display re-smoke are complete. Remaining hardware-facing work continues with
Phase 20 USB recovery/queue pressure and guarded web/profile/OTA mutations,
controller-profile replacement/recovery and focused Flanger/Delay
audio/target/timing/depth behavior. Remaining product work also includes
selected shifted-control rows still marked in the MIDI map, physical acceptance
of the host-qualified Hercules Inpulse 500 non-FLX4 profile, production
signing-key provisioning, enclosure
power/thermal/RF soak, and a full end-to-end P4/FLX4 regression pass.
(Beat feedback from the PQTZ
beatgrid is already shown on the Overview beat strip with a red downbeat marker;
a dedicated controller beat LED was declined, so that item is closed.)

## Multi-Controller Platform

A data-driven controller-profile platform lets the system support controllers
beyond the DDJ-FLX4 without a firmware rebuild. Profiles live on the SD/TF card
(`/controllers/<name>/profile.s3bin`); the P4 matches the connected controller
by VID/PID and activates the profile locally for MIDI in/out (the built-in FLX4
map is the fallback). A compiled profile can also be atomically overwritten from the P4
Wi-Fi Remote; the P4 validates it, preserves a recoverable backup, rescans and
activates the matching profile locally without physical SD access. The
firmware side (Phases 1–7 + 11 of the multi-controller plan) is
implemented, host-tested, and hardware-verified on the profile-loading path;
see [ARCHITECTURE.md](ARCHITECTURE.md) and
[CONTROLLER_PROFILE_SCHEMA.md](CONTROLLER_PROFILE_SCHEMA.md). The web overwrite
path is software-complete and its hardware procedure is in
[CONTROLLER_PROFILE_UPDATE.md](CONTROLLER_PROFILE_UPDATE.md). Out of firmware
scope: the Windows Profile Builder tool. The committed Hercules DJControl
Inpulse 500 profile is now compiler/registry/runtime/LED/P4-behavior
host-qualified using the official MIDI specification, including exact
`06F8:B12B` matching, RGB pads, Shift+Sync Off and autoloop rotary semantics.
Physical controller and USB-audio acceptance remain open; see
[HERCULES_INPULSE_500_MIDI_MAP.md](HERCULES_INPULSE_500_MIDI_MAP.md).
