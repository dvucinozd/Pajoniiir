# DDJ-FFL4 Project Overview

Status: current product overview, audited 2026-07-13. The inherited baseline
below is historical context; `Current Port Status` describes the active system.

## Goal

DDJ-FFL4 is a standalone two-deck DJ player using the Pioneer DDJ-FLX4 as the
operator surface. It does not run Mixxx and it does not require a PC during
performance. The DDJ-FLX4 supplies controls and LEDs; the ESP32 boards provide
media browsing, playback, audio output, and display.

The project starts as a fork-style port of CDJ100S-XXX because that codebase
already proves the most important hardware and firmware surfaces on the target
boards.

## Product Shape

The system is split into two firmware targets:

- `firmware/control-board-s3`: USB MIDI host and protocol translator, FLX4
  USB-headphone output bridge, runtime diagnostics and S3 OTA service.
- `firmware/main-deck-p4`: media library, dual deck engine, UI, audio mixer.

The S3 stays non-authoritative. It reads raw FLX4 MIDI input, maps it to
semantic events, forwards those events over UART, mirrors P4 LED feedback back
to the FLX4 and streams P4 monitor PCM to the controller headphones. Its
service AP exposes logs and S3 OTA only when P4 requests it. It must not own
playback state.

The P4 owns all authoritative deck state. It loads Rekordbox media from USB,
tracks current position, controls audio decode, drives the local display, and
decides which LEDs should be on.

## Baseline Inherited From CDJ100S-XXX

The imported upstream code already provides:

- ESP32-P4 JC4880 board support for ST7701S display, GT911 touch, ES8311 audio,
  USB mass storage, and SD card cache/config.
- Rekordbox `export.pdb` and `ANLZ0000.DAT` parsing.
- A single-deck `deck_core` state machine.
- A single-track `audio_engine` with MP3 preload, minimp3 decode, pitch
  resampling, instant seek, hot cue, beat jump, and loop support.
- An ESP32-S3 firmware target with UART `control_link`.
- PC unit tests for parsers, audio engine, control link, and selected UI logic.

## Current Port Status

The fork is no longer only the imported single-deck baseline:

- S3 USB MIDI host logging exists, USB descriptor parsing is hardened, and the
  software translator maps FLX4 MIDI messages into deck-aware `control_link`
  frames behind `CONFIG_DDJ_FLX4_TRANSLATE_TO_P4`. R5D retired the inherited
  CDJ GPIO panel/TinyUSB-device path; USB host ownership is unconditional.
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
  deck-local three-band EQ, and Deck 2 producer support. Stereo Master and Split Mono cue/PFL routing are
  implemented for the current output path.
- P4 LVGL UI is dual-deck: Overview, Library load paths, performance target
  selection, Settings, status/header, and waveform rendering are split into
  smaller UI modules.
- Both deck Overview waveforms use the direct PPA overlay path (the 2026-06-13
  Deck 2 jitter that once forced an LVGL path is resolved). The waveform renders
  with the "Punchy" colour scheme (bright cyan transients, white transient tips),
  an active/armed loop-region amber highlight with edge markers, hot-cue markers
  on the large + mini waveforms, and a translucent played-progress overlay on the
  mini — all baked into the scrolling RGB565 strip so they PPA-blit without
  LVGL-over-PPA flicker.
- ESP-Hosted Wi-Fi is re-enabled behind a Settings switch (default off): the
  onboard ESP32-C6 provides a WPA2 SoftAP (`PAJONIIR`) and P4 serves a mobile web
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
- FLX4 Play/Cue/PFL LED MIDI output is implemented through P4-confirmed
  control-link feedback and the S3 USB MIDI Out queue. P4 also drives selected
  pad-mode LEDs, Beat Sync state LEDs, Loop In/Out LEDs from pending marker and
  active loop state, normal Hot Cue pad LEDs, normal Beat Loop pad LEDs, and VU
  meter output.
- S3 publishes DDJ-FLX4 USB connection state to P4, and P4 forces a P4-owned
  LED snapshot on reconnect. Hardware verification on 2026-06-20 confirmed
  Play/Cue/PFL LED recovery without playback or deck-state changes; extended
  pad-mode/sync/loop reconnect smoke has covered USB replug and S3 reset, and
  P4-only reset recovery is implemented through S3 heartbeat connected-state
  refresh with hardware smoke passed on 2026-06-26.
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
- The DDJ-FLX4 headphone jack now outputs the P4 cue/monitor mix over USB Audio
  Class. The S3 stays the FLX4 USB host and streams the P4 `hp_out` bus to the
  controller's physical headphones output, while PCM5102A remains the RCA MAIN
  OUT, so CUE/MONITOR (USB headphones) and MASTER (RCA) run simultaneously. The
  P4 ships the monitor PCM to the S3 over an inter-board I2S link, and ES8311 was
  dropped to free an I2S unit (the P4 usable I2S budget is two units on eco2
  silicon). Merged to `master` and hardware-verified end-to-end on 2026-07-02.
  A 2026-07-09 S3 regression fix keeps the FLX4 USB Audio stream's endpoint
  rate and packetizer aligned with the active P4 link rate after ring streaming
  has already started; the follow-up COM6 smoke held `P4_AUDIO_LINK overruns=0`,
  `gaps=0`, `crc=0`, and `FLX4_USB_AUDIO skipped=0 underrun=0` for roughly two
  minutes of product playback.
- The Beat FX audio DSP was overhauled on 2026-07-10 for better sound and less
  touchy knob response: the one-knob channel Filter became a resonant ZDF
  state-variable filter with an exponential low-pass/high-pass sweep to full
  kill, the Echo gained per-generation feedback damping and a ring-out tail,
  Smart CFX moved to a smoothstep response curve, and a beat-synced **Flanger**
  was added as a third Beat FX effect (cycle FILTER → ECHO → FLANGER). The
  Overview Beat FX rail was redesigned to be effect-colour-coded
  (Filter blue / Echo amber / Flanger magenta) with a vertical depth meter.
- On 2026-07-16, **Delay** was added as Beat FX value `4` without renumbering
  the existing effects. It is a BPM-synchronized, full-band one-shot repeat
  without feedback; Level/Depth controls its wet gain. Delay and Echo reuse the
  same per-deck stereo delay line, so the new mode does not allocate additional
  PSRAM, while Echo keeps its damped multi-repeat feedback behavior. The
  selector now cycles `FILTER → ECHO → FLANGER → DELAY → FILTER`, with the
  reverse order on Previous and no `NONE` slot in either direction. Software
  acceptance is covered by the P4 host suites and `idf.py build`; physical
  DELAY sound/target/beat/depth smoke remains pending.
- The FLX4 USB-headphone audio profile was made the default build on both boards
  (folded into each `sdkconfig.defaults` on 2026-07-10), so a plain `idf.py
  build` now produces the sound firmware; the per-profile overlays and stale
  `build_*` dirs were removed.
- The seven official DDJ-FLX4 MIDI gaps were closed against the official Pioneer
  MIDI message list: Censor, Sync (set-master), Quantize, Loop Adjust In/Out,
  Reloop+Stop, Headphone Level, and the shifted Browse/Load/Beat-FX controls.
  The S3 maps the new physical MIDI notes to semantic control-link events and the
  P4 owns the resulting state/audio/LED behavior. Censor is an approximation
  (forward seek-back, sync-correct on release) rather than a true reverse.
  Merged to `master` on 2026-07-02; host suites pass. The XIAO RC2 smoke on
  2026-07-07 confirmed the connected FLX4 headphone/mixer control path with
  audible USB headphone output; remaining per-control exceptions stay tracked in
  `docs/DDJ_FLX4_MIDI_MAP.md`.
- The FLX4 USB headphones and official-MIDI-gap-closure feature branches were
  merged to `master` and deleted after the 2026-07-02 work. On 2026-07-03 the
  remaining stale Codex branches were reviewed and removed (local + remote),
  including the old experimental `codex/flx4-extended-controls`, whose verified
  slices had already been salvaged into `master`. No non-master feature branches
  remain.
- Vinyl/scratch is hardware-validated on both decks: platter touch selects a
  canonical PSRAM PCM timeline for forward/reverse scratch, including paused
  and CUE states, active-loop wrapping, clean window edges and click-free
  release/re-grab.
- Master Tempo/key lock is implemented in the P4 audio callback and exposed by
  the Overview `MT` control. Basic pitch/key behavior passed hardware smoke;
  longer simultaneous-deck quality and CPU tuning remains an optimization item.
- OTA is hardware-accepted for both processors. P4 and S3 use alternating OTA
  slots, validate target/chip/project, confirm health after mandatory startup,
  preserve the active image on interrupted upload and roll back an unconfirmed
  image. The accepted 2026-07-13 release was `RC1-106-g717b6ab3` on both boards.

## Non-Goals For The First Milestone

- Full Mixxx feature parity.
- Continuous beat following or full Rekordbox-style sync state. Master Tempo
  key lock is implemented; advanced phase-vocoder quality work remains outside
  the first milestone.
- Deeper Beat FX and Pad FX hardware acceptance beyond the current Smart CFX
  smoothstep curve, Smart Fader transition-assist V1 behavior, the Beat FX
  Filter/Echo/Flanger hardware baseline, the software-covered Delay mode, and
  the host-tested Pad FX DSP/input/LED slice.
- Four-deck support.
- Rekordbox library editing.
- Running JavaScript Mixxx mappings on-device.
- Treating FLX4 MIDI XML as executable logic.

## Proposed Next Work

Two design plans have since been implemented in firmware (details in Phase 8 of
[DEVELOPMENT_PLAN.md](DEVELOPMENT_PLAN.md)):

- **S3 XIAO GPIO21 user status LED** — implemented as the S3 `status_led`
  component for FLX4 host modes. It uses the active-low XIAO onboard user LED
  for reduced USB/link/activity feedback; it does not encode playback state.
- **WAV + FLAC playback** — implemented through the decoder-abstraction layer
  and firmware preload path. MP3 keeps PVBR seek support; WAV/FLAC use decoder
  metadata while Rekordbox/ANLZ still supplies beatgrid/BPM/waveform context.
- **Wi-Fi web UI mobile controller** — implemented (ESP-Hosted SoftAP, Settings
  toggle, captive portal). Remaining web-UI scope (EQ/filter/hot-cue/beat-jump
  controls, waveform) is intentionally deferred — the web UI stays a simple
  remote, not a full control surface.
- **Overview waveform visualisations** — implemented ("Punchy" colours, loop
  highlight, hot-cue markers, mini played-progress).

Remaining hardware-facing items: selected shifted-control hardware acceptance
where still marked in the MIDI map, enclosure power/thermal/RF soak and a full
end-to-end S3/P4/FLX4 regression pass after the next major control or UI batch.
(Beat feedback from the PQTZ
beatgrid is already shown on the Overview beat strip with a red downbeat marker;
a dedicated controller beat LED was declined, so that item is closed.)

## Multi-Controller Platform

A data-driven controller-profile platform lets the system support controllers
beyond the DDJ-FLX4 without a firmware rebuild. Profiles live on the SD/TF card
(`/controllers/<name>/profile.s3bin`); the P4 matches the connected controller
by VID/PID and transfers the profile to the S3 over the UART `0xA6` bulk layer,
which then maps controller MIDI in/out through it (built-in FLX4 map is the
fallback). A compiled profile can also be atomically overwritten from the P4
Wi-Fi Remote; the P4 validates it, preserves a recoverable backup, rescans and
queues the matching profile for S3 activation without physical SD access. The
firmware side (Phases 1–7 + 11 of the multi-controller plan) is
implemented, host-tested, and hardware-verified on the profile-loading path;
see [ARCHITECTURE.md](ARCHITECTURE.md) and
[CONTROLLER_PROFILE_SCHEMA.md](CONTROLLER_PROFILE_SCHEMA.md). The web overwrite
path is software-complete and its hardware procedure is in
[CONTROLLER_PROFILE_UPDATE.md](CONTROLLER_PROFILE_UPDATE.md). Out of firmware
scope: the Windows Profile Builder tool and validating a first non-FLX4
controller end-to-end.
