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
  existing UI library load flow; Deck 2 returns `ESP_ERR_NOT_SUPPORTED` until
  the dual audio/load backend exists.

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
- Deck 0 remains the firmware output compatibility deck. Deck 1 firmware output
  remains blocked until per-deck decode/output tasks and the mixer are added.
- `deck_core` now calls the deck-aware audio API instead of direct singleton
  audio calls.
- `audio_mixer` provides host-tested channel fader gain, center-open
  crossfader gains, stereo summing, single-frame gain application, and int16
  saturation.
- `audio_engine` now stores channel volume and crossfader state, exposes output
  gain calculation, and `deck_core` routes `CTRL_ID_CH1_VOLUME`,
  `CTRL_ID_CH2_VOLUME`, and `CTRL_ID_CROSSFADER` into that state.
- The firmware output task applies the Deck 0 master gain in the compatibility
  path. True dual-deck summing is still pending until Deck 1 has a firmware PCM
  producer/output path.
- PCM ring storage is now a host-tested `audio_pcm_ring` module, and
  `audio_engine` owns one ring per deck while the firmware compatibility path
  still consumes only Deck 0. This removes the old global singleton ring as a
  blocker for the next Deck 1 producer step.
- Pitch/resampler storage is now a host-tested `audio_resampler` module, and
  `audio_engine` owns one resampler state per deck while the firmware
  compatibility output still renders only Deck 0.
- The firmware output task now renders through a host-tested
  `audio_output_mixer` skeleton that accepts Deck 0 and Deck 1 sources, applies
  per-deck gains, and reports consumed frames per deck. Deck 1 remains silent
  until a firmware PCM producer is added.
- Firmware preload path/buffer/progress state is now a host-tested
  `audio_fw_preload` slot, with one slot allocated per deck.
- Firmware task lifecycle state is now a host-tested `audio_fw_runtime` slot,
  with one slot allocated per deck. The current firmware task creation path
  still starts only the compatibility producer/output task set, so Deck 1
  firmware producer activation remains pending.
- Firmware task arguments are now bound through a host-tested
  `audio_fw_task_context` slot. Loader/decode/output tasks receive explicit
  preload/runtime state, plus deck-local engine/ring/resampler slots, instead
  of looking up the active deck while running.
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

- show Deck 1 and Deck 2 loaded tracks;
- show positions, BPM, pitch, play/cue states;
- show mixer/cue status;
- keep large waveform work secondary until audio engine is stable.

Exit criteria:

- operator can confirm both deck states without using serial logs;
- UI does not drive authoritative playback decisions.
