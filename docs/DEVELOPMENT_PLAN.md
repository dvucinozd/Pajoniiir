# Development Plan

## Phase 0: Baseline Import And Documentation

Status: started.

Deliverables:

- import CDJ100S-XXX codebase;
- vendor the FLX4 Mixxx XML mapping under `docs/reference/`;
- document architecture, MIDI map, protocol, wiring, risks, and startup tasks;
- run a repository sanity check.

## Phase 1: S3 USB MIDI Host Spike

Status: raw logger implemented; hardware capture still required.

Goal: prove that the S3 can enumerate the DDJ-FLX4 and read raw MIDI.

Tasks:

- add `flx4_midi_host` component; done
- initialize ESP-IDF USB host; done
- log device VID/PID, interfaces, endpoints, and MIDI packets; implemented, pending hardware capture
- create a raw MIDI capture mode;
- verify Play, Cue, Load, Browse, jog, tempo, channel faders, crossfader, and
  headphone cue against `docs/DDJ_FLX4_MIDI_MAP.md`.

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

Goal: split the single-deck state model into two deck instances.

Tasks:

- adapt P4 `control_link` parser to route deck-aware IDs;
- refactor `deck_core` state into `deck_state[2]`;
- expose deck-specific load/play/cue/jog/pitch APIs;
- preserve old single-deck behavior behind compatibility helpers only where
  needed during transition;
- update tests for both decks.

Exit criteria:

- Deck 1 and Deck 2 Play/Cue state can be changed independently;
- pitch values are stored independently;
- UI snapshots can read both deck states.

## Phase 4: Dual Audio Engine And Mixer

Goal: play two tracks and mix them into a master output.

Tasks:

- split single global `s_eng` into per-deck engine state;
- run two decode producers;
- add a mixer/output consumer;
- implement channel volume and crossfader gains;
- add clipping-safe summing;
- produce cue/PFL buffer path after master mix is stable.

Exit criteria:

- two loaded MP3s decode without watchdog resets;
- master output can mix both decks;
- channel faders and crossfader affect gain correctly;
- CPU, SRAM, and PSRAM margins are documented.

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
