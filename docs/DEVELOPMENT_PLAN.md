# Development Plan

## Phase 0: Baseline Import And Documentation

Status: complete.

Deliverables:

- import CDJ100S-XXX codebase;
- vendor the FLX4 Mixxx XML mapping under `docs/reference/`;
- document architecture, MIDI map, protocol, wiring, risks, and startup tasks;
- run a repository sanity check.

## Phase 1: S3 USB MIDI Host Spike

Status: complete.

Goal: prove that the S3 can enumerate the DDJ-FLX4 and read raw MIDI.

Tasks:

- add `flx4_midi_host` component; done
- initialize ESP-IDF USB host; done
- log device VID/PID, interfaces, endpoints, and MIDI packets; done
- create a raw MIDI capture mode; done
- reject malformed/truncated USB descriptors before claiming endpoints; done
- add Windows host regression coverage for MIDI packet parsing and endpoint
  selection; done
- verify Play, Cue, Load, Browse, jog, tempo, channel faders, crossfader, and
  headphone cue against `docs/DDJ_FLX4_MIDI_MAP.md`; done

Validation note, 2026-06-14:

- S3 was successfully flashed on `COM3`.
- By increasing `CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE=512`, the large configuration descriptors of Pioneer DDJ-FLX4 are now successfully parsed.
- Boot log confirms successful enumeration on native OTG port.
- Captured raw MIDI packets for all MVP controls (Play/Pause, Cue, Load, Browse, Faders, Pitch, PFL) match the mapping in `docs/DDJ_FLX4_MIDI_MAP.md` exactly.

Exit criteria:

- raw MIDI logs match the XML for MVP controls; done
- S3 remains stable with FLX4 connected for at least 30 minutes; done
- no P4 dependency is required for the capture test; done

## Phase 2: S3 MIDI-To-Control-Link Translation

Status: complete.

Goal: send deck-aware semantic frames from S3 to P4.

Tasks:

- create `flx4_map.h`; done
- implement MSB/LSB coalescing for 14-bit controls; done
- map MVP controls to the IDs in `docs/CONTROL_LINK_PROTOCOL.md`; done
- keep heartbeat behavior in translator mode; done
- add PC tests or host-side unit tests for mapping logic where possible; done
- coalesce high-rate jog/tempo/fader values before UART transmission; done
- keep raw logger and translator as separate firmware modes; done

Exit criteria:

- serial logs show correct 7-byte frames for all MVP controls; done
- no duplicate noisy frames from analog controls beyond a configured threshold; done
- P4 can still detect S3 connected/offline state; done

Validation note, 2026-06-14:

- `CONFIG_DDJ_FLX4_TRANSLATE_TO_P4` has been enabled by default in `sdkconfig.defaults`.
- S3 firmware now successfully maps the physical MIDI events to 7-byte `0xA5` control link UART packets.
- Heartbeat task is active, sending S3 online status updates to P4.


## Phase 3: P4 Deck-Aware Control Link And Deck Core

Status: substantially complete for the local P4 path; S3 real input remains
pending until FLX4 raw MIDI capture is proven.

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
- Later P4 UI/audio work verified Deck 1 and Deck 2 snapshots, active target
  selection, Deck 2 transport position sync, and deck-local Overview metadata
  for the local touchscreen path.

Exit criteria:

- Deck 1 and Deck 2 Play/Cue state can be changed independently;
- pitch values are stored independently;
- UI snapshots can read both deck states.

## Phase 4: Dual Audio Engine And Mixer

Status: master mix path implemented for the P4 local path; cue/PFL audio output
buffer remains pending.

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
- `audio_engine` stores `s_engines[2]` with deck-local lifecycle state,
  load progress, and last-error reporting. `audio_engine_deck_get_status()`
  is the UI-safe per-deck status API; the legacy singleton status functions are
  compatibility wrappers for Deck 1.
- Firmware output/codec ownership is now a shared output service, not a deck
  runtime responsibility. Per-deck loads start loader/decode producer tasks;
  one shared output task mixes both PCM rings and closes the codec only when
  all decks are stopped.
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
- Firmware task creation now uses a host-tested `audio_fw_task_plan`.
  Deck-local task plans are producer-only loader/decode plans; shared output
  task startup is handled separately by `audio_engine`.
- The P4 Library screen exposes `LOAD D1` and `LOAD D2` buttons. `LOAD D1`
  updates the active waveform/header and compatibility output path; `LOAD D2`
  exercises the deck-local producer path for the shared output mixer.
- `audio_engine` now stores per-deck PFL state, and `deck_core` routes
  `CTRL_ID_DECK1_PFL` and `CTRL_ID_DECK2_PFL` press events into deck-specific
  PFL toggles. The actual cue/headphone audio buffer path remains pending.
- Deck 2-first playback no longer depends on whichever deck loaded first owning
  output. `deck_core` only marks a deck playing after the backend accepts play,
  and USB removal now calls `audio_engine_stop_all()`.

Validation note, 2026-06-14:

- `tests/run_p4_host_tests.ps1` is the primary Windows host regression runner
  when `make` is not available.
- `deck_core` no longer holds its state mutex while calling audio/UI APIs, and
  P4 UART RX coalesces high-rate JOG/PITCH events when the queue is full.
- `media_catalog_load_from_source()` and `media_catalog_get_from_source()` keep
  UI load workers pinned to the source captured at submit time. JOINED library
  refresh now runs in a worker task and reports back to the LVGL update loop.
- PDB raw file buffers prefer PSRAM on firmware builds, PDB truncation is
  logged explicitly, and ANLZ PQTZ beat allocation is capped before allocation.
- `sdkconfig.defaults` no longer assigns the unknown
  `CONFIG_ESP32P4_SELECTS_REV_LESS_V3` symbol under ESP-IDF v5.5.

## Phase 5: LED Feedback

Status: complete.

Goal: reflect P4-confirmed state on the FLX4.

Tasks:

- define LED IDs for Deck 1/2 Play, Cue, and PFL; done
- send LED commands from P4 after state changes; done
- translate LED commands to FLX4 MIDI output on S3; done
- verify LED behavior on hardware. done

Validation note, 2026-06-14:

- S3 has an asynchronous USB MIDI Out queue and mutex synchronization for safe packet delivery to DDJ-FLX4.
- S3 UART receiver extracts deck index from `val_hi` (byte 4) and forwards Note On/Off packets to the controller (Play, Cue, PFL).
- P4 periodically updates and transmits LED frames for both Deck 1 and Deck 2 independently using a two-dimensional state cache.
- PC unit tests and both target builds pass successfully.

Exit criteria:

- FLX4 Play LEDs follow actual P4 playback state; done
- LEDs recover after S3 reconnect/heartbeat recovery. done

## Phase 6: Dual-Deck LVGL UI

Status: complete for the current P4 touchscreen/UI scope.

Goal: adapt the P4 display from single-deck CDJ UI to DDJ-FFL4 dual-deck status.

Tasks:

- show Deck 1 and Deck 2 loaded tracks; done
- show positions, BPM, pitch, play/cue states; done
- show mixer/cue status; done
- keep large waveform work secondary until audio engine is stable; done through
  scheduled redraws and module-level renderer isolation.

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
- Functional GUI element pass is complete for the current scope: performance
  tabs expose utility/status strips, Key Shift has D1/D2 targeting and visible
  tempo/key/pitch controls, Settings has a mixer/PFL routing block, and Overview
  owns per-deck cue marker objects for D1 and D2.
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
- Main waveform solid-cyan failure was traced to the second full-height beat-grid
  overlay: normal-length Rekordbox beat lists can cover every canvas column.
  Overview now draws only sparse white downbeat/bar guides on top of the waveform.
- Overview waveform model now prefers Rekordbox `PWV3` high-resolution waveform
  data when present and falls back to `PWAV` low-resolution data only when needed.
- The large D1/D2 Overview waveform is no longer a compressed full-track view:
  it renders a 16-beat zoom window around the current playhead with beat/downbeat
  grid lines. The lower mini waveform remains the full-track overview/position
  strip, and tap-to-seek/cue markers now use the visible zoom-window coordinate
  system.
- Follow-up polish enlarges the main waveform canvas and maps Rekordbox waveform
  color hints plus amplitude fallback into a Pioneered-like palette: hot pink,
  blue, cyan, green, amber, purple, and white downbeat/grid markers.
- Beat-grid clarity pass separates grid colors from waveform colors: downbeats
  remain bright full-height markers, while regular beat guides are short, dim
  grey lines so they aid beatmatching without hiding transients.
- Overview now includes a compact D2-vs-D1 phase meter between the main
  waveforms. It uses the shared beat-grid/BPM helper, shows signed beat offset,
  and turns green when the two decks are within the current lock tolerance.
- Deck 2 transport follow-up: firmware task planning now lets whichever deck is
  loaded first own the shared output task/codec path. This fixes the D2-first
  play path where Deck 2 could decode as a producer but had no active output
  consumer. `deck_core` also no longer marks a deck as playing when the audio
  backend rejects the play request, and D2 snapshots now sync position from the
  audio engine so the Overview waveform advances while Deck 2 is playing.
- Overview redraw now keeps the deck-local loaded low-resolution waveform as a
  fallback when no `PWV3` high-resolution data is present, so zoom-window redraw
  can still follow Deck 2 playback on tracks with only low waveform data.
- Smooth Overview motion pass moved main waveform redraw cadence into a
  host-tested helper. Playing decks refresh the zoom window at roughly 30 fps,
  while paused decks keep the coarser 80 ms cadence.
- Overview renderer refactor extracted indexed-pixel main/mini waveform drawing
  from `ui.c` into a host-tested `ui_overview_renderer` module. Later refactor
  phases moved LVGL screen construction and callbacks into `ui_overview`, so
  `ui.c` is now an 887-line top-level orchestrator.
- Overview render timing probe logs aggregated main waveform render cost per
  deck every 60 redraws (`last/avg/max/samples`). Use this data to decide
  whether the next optimization should target full-canvas redraw cost, LVGL
  invalidation/copy cost, or playback position cadence.
- Overview waveform fluidity pass adds a host-tested UI position interpolator.
  Deck snapshots remain authoritative, but while a deck is playing the Overview
  screen advances display position between backend snapshots using the UI
  monotonic clock and current pitch speed, then rebases on cue/seek/large drift.
- Follow-up diagnostics log `ui_update` interval/duration and LVGL handler
  interval/duration every 60 samples. This separates slow redraw causes between
  application update cadence, LVGL full-frame handling, and waveform renderer
  cost.
- Hardware timing pass on 2026-06-12 confirms the indexed main waveform
  renderer is not the main fluidity bottleneck: D1/D2 main render averages were
  roughly 1.7-2.2 ms, while the live overlay path averaged roughly 7.2-8.3 ms
  per deck. Most of that cost is the I8-to-RGB565 conversion at roughly 4-5 ms
  per deck, followed by PPA rotation/copy at roughly 2.4-2.6 ms per deck.
  Dual-deck live overlay therefore costs about 15-17 ms per UI update before
  LVGL full-frame handler spikes. Next optimization should remove conversion
  from the hot path by rendering the overview overlay directly into an RGB565
  source buffer, or narrow the live zoom surface to a single active deck if
  dual live overlays remain too expensive.
- Follow-up RGB565 pass on 2026-06-15 removes the I8-to-RGB565 conversion from
  the steady Overview waveform path. The main waveform now uses a per-deck
  RGB565 cache with host-tested column rendering, source/window invalidation,
  and subpixel scroll accumulation. COM15 diagnostics confirm the original
  11-12 ms full-redraw path is gone, but the current cache still physically
  scrolls the whole 648x141 RGB565 buffer before blitting. Dual-deck steady
  playback therefore remains limited by CPU buffer scroll plus full overlay PPA
  copy: measured `overview main cache` was still roughly 5.5-6.5 ms and
  `overview overlay total` roughly 4.8-5.2 ms per updated deck. The next
  fluidity fix should avoid full-buffer CPU scrolling, either with a
  framebuffer/overlay PPA scroll-and-edge-fill path or a wider/circular RGB565
  strip that can be blitted by source offset without moving the whole cache.
- Follow-up zero-copy scroll pass on 2026-06-16 implements the circular RGB565
  strip path. Each Overview deck now owns a wider RGB565 strip and reports one
  or two source-region PPA blit segments for the visible window. Steady scroll
  advances by changing the source offset (`UI_OVERVIEW_WAVE_CACHE_OFFSET`) with
  `columns_rendered == 0`; bounded `EDGE` updates render only newly exposed
  columns, and full strip rebuilds are reserved for load/source/window changes
  or large seeks. Host tests cover offset-only updates, bounded edge batches,
  wrapped two-segment blits, source-region geometry validation, and the guard
  that `ui_overview_wave_cache.c` must not use `memmove(`.
- Zero-copy runtime validation status: P4 build and COM15 flash passed on
  2026-06-16. A diagnostics run with both decks playing confirmed the cache
  fix: steady `OFFSET` updates averaged about 10 us per deck with
  `columns_rendered == 0`, and bounded `EDGE` updates averaged about 0.5 ms for
  32 rendered columns. No panic, watchdog timeout, brownout, or unexpected reset
  appeared in the capture. The remaining visible budget is no longer CPU cache
  scrolling; each deck still spends roughly 4.0 ms in overlay PPA copy, while
  LVGL render/refr windows still spike into roughly 30-35 ms ranges. The next
  fluidity pass should reduce dual overlay blit cost and/or decouple the main
  waveform overlay cadence from full LVGL invalidation/render spikes.
- Follow-up mini-wave invalidation pass limits the lower mini waveform progress
  update to the changed column range instead of invalidating the full 392x45
  canvas. COM15 diagnostics confirmed the previous `17640 px` max invalidated
  area disappears. The largest repeated invalidated area is now `10584 px`, so
  diagnostics were extended to log `max_area=(x,y wxh)` for the next capture
  with both decks playing.
- P4 UI architecture refactor closed on 2026-06-13. Extracted modules include
  `ui_lvgl_backend`, `ui_overview`, `ui_library`, `ui_controls`,
  `ui_performance_tabs`, `ui_settings`, `ui_status`,
  `ui_overview_renderer`, `ui_overview_scheduler`, and frame-context helpers.
  `ui.c` owns init, screen registry, top-level tab switching, and frame context
  construction.
- Deck 2 lower Overview waveform jitter was fixed on 2026-06-13. The scheduler
  now allows a two-deck redraw budget when both decks are playing, but direct
  PPA overlay is allowed only for Deck 1. Deck 2 uses the normal LVGL
  invalidate/flush path, which visually removed the lower Deck 2 waveform
  jitter on hardware.
