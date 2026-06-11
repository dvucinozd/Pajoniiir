# Development Plan

## Phase 0: Baseline Import And Documentation

Status: started.

Deliverables:

- import CDJ100S-XXX codebase;
- vendor the FLX4 Mixxx XML mapping under `docs/reference/`;
- document architecture, MIDI map, protocol, wiring, risks, and startup tasks;
- run a repository sanity check.

## Phase 1: S3 USB MIDI Host Spike

Status: raw logger implemented and flashed; hardware capture deferred.

Goal: prove that the S3 can enumerate the DDJ-FLX4 and read raw MIDI.

Tasks:

- add `flx4_midi_host` component; done
- initialize ESP-IDF USB host; done
- log device VID/PID, interfaces, endpoints, and MIDI packets; implemented,
  pending FLX4 enumeration
- create a raw MIDI capture mode; done
- verify Play, Cue, Load, Browse, jog, tempo, channel faders, crossfader, and
  headphone cue against `docs/DDJ_FLX4_MIDI_MAP.md`.

Validation note, 2026-06-08:

- S3 was flashed on `COM3` with app version `fd663e6`.
- Boot log confirms `DDJ-FLX4 USB MIDI host raw logger started`.
- FLX4 did not enumerate during hotplug or reset-with-device-connected capture.
- Next S3 validation session should focus on physical USB host bring-up:
  native OTG port, powered hub orientation, 5 V VBUS, and shared ground.
- P4 work may proceed in parallel, but S3-to-P4 semantic event integration must
  wait until raw FLX4 MIDI capture is proven.

Exit criteria:

- raw MIDI logs match the XML for MVP controls;
- S3 remains stable with FLX4 connected for at least 30 minutes;
- no P4 dependency is required for the capture test.

## Phase 2: S3 MIDI-To-Control-Link Translation

Goal: send deck-aware semantic frames from S3 to P4.

Tasks:

- create `flx4_map.h`;
- implement MSB/LSB coalescing for 14-bit controls;
- map MVP controls to the IDs in `docs/CONTROL_LINK_PROTOCOL.md`;
- keep heartbeat behavior;
- add PC tests or host-side unit tests for mapping logic where possible.

Exit criteria:

- serial logs show correct 7-byte frames for all MVP controls;
- no duplicate noisy frames from analog controls beyond a configured threshold;
- P4 can still detect S3 connected/offline state.

## Phase 3: P4 Deck-Aware Control Link And Deck Core

Status: started; control-link semantic ID baseline implemented.

Goal: split the single-deck state model into two deck instances.

Tasks:

- adapt P4 `control_link` parser to route deck-aware IDs; started
- refactor `deck_core` state into `deck_state[2]`; started
- expose deck-specific load/play/cue/jog/pitch APIs; snapshot/reset/load started
- preserve old single-deck behavior behind compatibility helpers only where
  needed during transition;
- update tests for both decks.

Validation note, 2026-06-08:

- P4 baseline build passes for target `esp32p4`.
- `control_link` now defines DDJ-FLX4 deck-aware IDs for Deck 1, Deck 2,
  mixer, browser, and system namespaces.
- UART parser fills `ctrl_event_t.deck` and `ctrl_event_t.control` for
  deck-aware IDs while preserving existing single-deck encoder IDs.
- `deck_core` now stores two deck states and accepts deck-aware Play, Cue, jog,
  and pitch events independently. Deck 1 remains connected to the current
  single global `audio_engine`; Deck 2 is state-only until Phase 4 dual audio.
- Added a host-side `deck_core_dual` test for independent Deck 1/Deck 2
  transport and pitch state.
- Browser namespace routing now accepts `CTRL_ID_BROWSE_DELTA`,
  `CTRL_ID_LOAD_DECK1`, and `CTRL_ID_LOAD_DECK2`. Deck 1 load delegates to the
  UI library load flow; Deck 2 now targets the deck-aware UI load flow and is
  limited by the current producer-only Deck 2 audio backend.

Exit criteria:

- Deck 1 and Deck 2 Play/Cue state can be changed independently;
- pitch values are stored independently;
- UI snapshots can read both deck states.

## Phase 4: Dual Audio Engine And Mixer

Status: started; deck-aware API and per-deck engine state storage implemented.

Goal: play two tracks and mix them into a master output.

Tasks:

- add deck-aware audio API boundary; done
- split single global `s_eng` into per-deck engine state; started
- run two decode producers;
- add a mixer/output consumer;
- implement channel volume and crossfader gains; math layer done
- add clipping-safe summing; math layer done
- produce cue/PFL buffer path after master mix is stable.

Exit criteria:

- two loaded MP3s decode without watchdog resets;
- master output can mix both decks;
- channel faders and crossfader affect gain correctly;
- CPU, SRAM, and PSRAM margins are documented.

Validation note, 2026-06-08:

- `audio_engine_deck_*` APIs exist as the boundary between `deck_core` and the
  dual-engine backend.
- `audio_engine` stores `s_engines[2]` and routes deck-aware operations through
  the selected engine state in the PC/test path.
- Deck 0 remains the firmware output compatibility deck and owns codec/output
  startup. Deck 1 can now participate in the shared output mixer once the
  compatibility output task is running.
- `deck_core` now calls the deck-aware audio API instead of direct singleton
  audio calls.
- `audio_mixer` provides host-tested channel fader gain, center-open
  crossfader gains, stereo summing, single-frame gain application, and int16
  saturation.
- `audio_engine` now stores channel volume and crossfader state, exposes output
  gain calculation, and `deck_core` routes `CTRL_ID_CH1_VOLUME`,
  `CTRL_ID_CH2_VOLUME`, and `CTRL_ID_CROSSFADER` into that state.
- The firmware output task reads both deck rings through `audio_output_mixer`
  and applies the current channel/crossfader gains.
- PCM ring storage is now a host-tested `audio_pcm_ring` module, and
  `audio_engine` owns one ring per deck. The shared firmware output path can
  consume both rings through the mixer.
- Pitch/resampler storage is now a host-tested `audio_resampler` module, and
  `audio_engine` owns one resampler state per deck.
- The firmware output task now renders through a host-tested
  `audio_output_mixer` path that accepts Deck 0 and Deck 1 sources, applies
  per-deck gains, and reports consumed frames per deck.
- Firmware preload path/buffer/progress state is now a host-tested
  `audio_fw_preload` slot, with one slot allocated per deck.
- Firmware task lifecycle state is now a host-tested `audio_fw_runtime` slot,
  with one slot allocated per deck.
- Firmware task arguments are now bound through a host-tested
  `audio_fw_task_context` slot. Loader/decode/output tasks receive explicit
  preload/runtime state, plus deck-local engine/ring/resampler slots, instead
  of looking up the active deck while running.
- Firmware task creation now uses a host-tested `audio_fw_task_plan`. The
  compatibility deck still starts loader/decode/output and owns codec open;
  Deck 1 load starts producer-only loader/decode tasks into its own PCM ring and
  is transport-supported for play/pause/seek/pitch through the shared output
  mixer.
- The P4 Library screen exposes `LOAD D1` and `LOAD D2` buttons. `LOAD D1`
  updates the active waveform/header and compatibility output path; `LOAD D2`
  exercises the deck-local producer path for the shared output mixer.
- `audio_engine` now stores per-deck PFL state, and `deck_core` routes
  `CTRL_ID_DECK1_PFL` and `CTRL_ID_DECK2_PFL` press events into deck-specific
  PFL toggles. The actual cue/headphone audio buffer path remains pending.

## Phase 5: LED Feedback

Goal: reflect P4-confirmed state on the FLX4.

Tasks:

- define LED IDs for Deck 1/2 Play, Cue, and PFL;
- send LED commands from P4 after state changes;
- translate LED commands to FLX4 MIDI output on S3;
- verify LED behavior on hardware.

Exit criteria:

- FLX4 Play LEDs follow actual P4 playback state;
- LEDs recover after S3 reconnect/heartbeat recovery.

## Phase 6: Dual-Deck LVGL UI

Goal: adapt the P4 display from single-deck CDJ UI to DDJ-FFL4 dual-deck status.

Tasks:

- show Deck 1 and Deck 2 loaded tracks; started
- show positions, BPM, pitch, play/cue states; started
- show mixer/cue status;
- keep large waveform work secondary until audio engine is stable.

Exit criteria:

- operator can confirm both deck states without using serial logs;
- UI does not drive authoritative playback decisions.

Validation note, 2026-06-10:

- Overview tab now uses two deck panels instead of the inherited single-deck
  view.
- Each panel shows deck-local title/artist, elapsed/remaining time, BPM, pitch,
  transport status, and a low-resolution Rekordbox waveform canvas.
- `LOAD D1` and `LOAD D2` update their own Overview waveform and metadata.
- The old full-width high-resolution zoom waveform was removed from Overview
  for this MVP; bring it back later as an active-deck detail view if SRAM and
  render budget allow it.
- P4 UI now stores deck-local ANLZ metadata snapshots, so loading Deck 2 no
  longer overwrites the metadata used by Deck 1 Overview waveform rendering.
- Hot Cue, Beat Loop, and Beat Jump screens now expose a D1/D2 target selector.
  These performance controls use deck-local ANLZ/BPM/position metadata and
  deck-aware audio seek/play/loop APIs.
- Header title/artist/time/BPM/pitch/status now follow the active D1/D2
  performance target.
- The legacy single-set `PLAY`, `CUE`, `BEAT`, and `END` LED feedback sent to
  S3 now represents the active D1/D2 performance target.
- Overview deck panels now expose mixer/PFL state from
  `audio_engine_get_mixer_snapshot`: raw channel fader percent, effective
  output gain percent, and deck-local PFL on/off state. The top deck panel also
  shows a global crossfader strip.
- P4 GUI visual direction is now aligned with the Pioneered/Pioneered-Plus
  reference: top navigation, black base surface, Pioneered color tokens, sharp
  rectangular controls, and a first-pass Overview layout with wide deck
  waveforms, deck-local title strips, and right-side mixer/transport blocks.
- Overview now reserves a Pioneered-style right FX/status panel and uses a more
  reference-like waveform treatment: white playhead, brighter vertical grid
  guides, downbeat markers, fallback grid when beat metadata is missing, and a
  lighter played overlay color.
- Functional GUI element pass is in progress before final polish: performance
  tabs now expose their utility/status strips, Key Shift has D1/D2 targeting and
  visible tempo/key/pitch controls, Settings has a mixer/PFL routing block, and
  Overview owns per-deck cue marker objects for D1 and D2.
- First Overview polish pass now follows the Pioneered reference more closely:
  the two deck waveforms live in the upper overview area, Beat FX is a compact
  right-side stack, and the deck metadata is split into two lower deck strips.
- Second Overview polish pass adds lower mini waveform strips, a centered
  playhead/CUE marker, and scaled Rekordbox low-waveform sampling so the wider
  overview canvases do not read past the 400-sample source buffer.
- Follow-up waveform visibility fix keeps the primary overview waveform canvas
  allocation ahead of mini waveform allocation and prefers PSRAM for mini strips
  so decorative buffers do not starve the main waveform canvas.
- Main waveform visibility was strengthened with a high-contrast Pioneered-like
  palette, source-amplitude normalization, and explicit foreground ordering for
  the primary waveform object.
- Main waveform height now uses the same Rekordbox low-5-bit amplitude field as
  the working mini waveform renderer; upper bits are treated as flags/color data
  and no longer inflate every column into a solid block.
