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

Status: substantially complete. Physical FLX4 MVP input and S3 translation
were verified on 2026-06-14.

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
  UI library load flow; Deck 2 targets the deck-aware UI load flow and the
  current shared output mixer path. Browse press toggles Library/Overview and
  does not load either deck.
- Later P4 UI/audio work verified Deck 1 and Deck 2 snapshots, active target
  selection, Deck 2 transport position sync, and deck-local Overview metadata
  for the local touchscreen path.

Exit criteria:

- Deck 1 and Deck 2 Play/Cue state can be changed independently;
- pitch values are stored independently;
- UI snapshots can read both deck states.

## Phase 4: Dual Audio Engine And Mixer

Status: master mix, channel fader/crossfader gains, cue/PFL selection, and
dual-deck P4 audio scheduling are implemented for the current two-deck P4 path.
Transparent post-sum limiting and limiter telemetry are implemented for the
current master output path. The limiter is a soft-knee post-sum stage: samples
below roughly ±30000 PCM units are unchanged, while hotter summed peaks are
compressed toward the int16 ceiling instead of hard-clipped. Limiter counters
are accumulated in the audio mixer snapshot and surfaced in the P4 status
indicator as `CLIP n` when the counter increases. The P4 audio output
late-warning diagnostic is calibrated to report true outliers instead of normal
blocking `esp_codec_dev_write()` pacing.

Goal: play two tracks and mix them into a master output.

Tasks:

- add deck-aware audio API boundary; done
- split single global `s_eng` into per-deck engine state; started
- run two decode producers; done for the current two-deck path
- add a mixer/output consumer; done
- implement channel volume and crossfader gains; math layer done
- add clipping-safe summing; math layer done
- add transparent post-sum limiting and limiter telemetry; done
- produce cue/PFL buffer path after master mix is stable; implemented for the
  current Stereo Master / Split Mono routing path.

TODO:

- Monitor the limiter telemetry during two-deck hardware smoke tests. 2026-06-30
  RCA smoke with soft-knee limiter passed subjectively (`Deck 1 OK`, `Deck 1 +
  Deck 2 OK`, RCA audio OK, `CLIP` only occasional) and objectively showed
  `active=1/1`, `late=0 late_max=0 us`, healthy PCM rings, and limiter activity
  rising only on hot summed peaks (`peak=36710`, `limiter=164`) in
  `logs/p4_dual_deck_soft_limiter_smoke_20260630_214519.log`. If future tests
  show constant limiter activity, use the user-facing master trim rather than
  silently lowering deck levels.
- Software master trim is now implemented in the audio engine as a non-boosting
  `0.0–1.0` global output scalar with default unity. The Settings screen exposes
  it as a preset button cycling `0 dB`, `-3 dB`, and `-6 dB`, with host-tested
  preset mapping, persists the selected preset through NVS, and reapplies it
  during P4 boot after `audio_engine_init()`. Use it only if hardware tests show
  constant limiter activity or audible clipping with normal mixer levels.
- P4 audio diagnostics are now available as a central audio-engine snapshot:
  output codec state/sample-rate, output late count/max/threshold, per-deck ring
  fill and active flags, per-deck decoded format/load metadata, limiter
  counters, and heap/internal/PSRAM free space. `/api/status` exposes the same
  data under `diagnostics` for structured smoke captures.
- Clean up the Overview waveform cache/render path and diagnostic leftovers.
  The current cache is already host-guarded for OFFSET/EDGE updates, no
  steady-path `memmove`, and bounded edge column rendering; further cleanup
  should be driven by hardware timing captures rather than blind renderer
  refactors. The cache also exposes per-instance profiling counters for
  follow-up measurement. 2026-06-30 RCA smoke confirmed audio is healthy while
  the operator still saw waveform stutter, so the next investigation should
  treat waveform fluidity as a UI/render scheduling issue, not an audio decode
  or PCM5102A issue. The first follow-up tried one deck per 16 ms UI tick, but
  hardware testing showed that the perceived stutter remained once the audio
  path was healthy. The scheduler now allows both playing deck waveforms to
  redraw in the same UI tick so each deck keeps full visual cadence.
  `FULL`, `OFFSET`, `EDGE`, and `NONE` updates plus total rendered columns and
  blits; the existing Overview diagnostics log includes those totals.
- Mixed 44.1/48 kHz dual-deck playback was fixed on 2026-07-01. Hardware
  diagnosis showed the OK pair France Gall + Comanchero were both 44.1 kHz,
  while the failing Men At Work + Caribbean Blue pair mixed 44.1 kHz and
  48 kHz. The output mixer now folds `source_sample_rate / output_sample_rate`
  into each deck's effective resampler step instead of using `pitch_factor`
  alone. Host regression coverage asserts that a 48 kHz deck on a 44.1 kHz
  output consumes 160 source frames per 147 output frames. Hardware smoke after
  the fix confirmed audio OK, fluid waveform, normal Caribbean Blue playback,
  and no reboot.
- Before adding PCM5102A hardware support, follow
  `docs/superpowers/plans/2026-06-26-pcm5102a-migration-readiness.md`: create
  a P4 pinout inventory, fix the output sample-rate strategy, split logical
  `master_out[]`/`hp_out[]` buffers, harden BSP audio init, and only then select
  approved P4 GPIOs for the PCM5102A.
- For the concrete DAC bring-up sequence, follow
  `docs/superpowers/plans/2026-06-26-pcm5102a-main-out-es8311-monitor.md`:
  use GPIO50/GPIO52/GPIO51 as the bench-verification candidate set, reject the
  GPIO22/GPIO23/GPIO24/GPIO25 proposal because GPIO23 is LCD backlight PWM, and
  route PCM5102A as MAIN OUT while keeping ES8311 as monitor/headphones/speaker.

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
- `audio_engine` stores per-deck PFL state, and `deck_core` routes
  `CTRL_ID_DECK1_PFL` and `CTRL_ID_DECK2_PFL` press events into deck-specific
  PFL toggles. Stereo Master and Split Mono cue/PFL routing are implemented
  for the current output path.
- Deck 2-first playback no longer depends on whichever deck loaded first owning
  output. `deck_core` only marks a deck playing after the backend accepts play,
  and USB removal now calls `audio_engine_stop_all()`.
- Dual-deck playback scheduling was stabilized on hardware on 2026-06-20 after
  runtime instrumentation showed healthy PCM ring fill and memory margins but
  output timing disruption during active preload/index work. The fix keeps
  active-output preload chunks to 32 KB, builds MP3 seek tables off to the side
  before publishing them under a short engine lock, removes the extra
  software delay after `esp_codec_dev_write()`, and throttles preload
  diagnostics to periodic summaries. User hardware confirmation reported
  normal audio and waveform behavior with both decks playing.
- Audio output diagnostic warning spam was fixed on hardware on 2026-06-21.
  The previous `diag output late` warning compared a block including
  blocking codec/I2S write time against a rounded 256-frame nominal period,
  causing normal ~10 ms codec pacing to look like a late block. The output
  timing helper now exposes a precise microsecond block period and a
  2x-period late-warning threshold. Hardware smoke with both decks playing
  reported `DIAG_OUTPUT_LATE_COUNT=0`, healthy ring fill, and stable decode
  timing while retaining aggregate `diag output` telemetry.
- PCM5102A MAIN OUT bring-up was hardware-smoked on 2026-06-27 with the
  photographed PCM5102MK/PCM5102A board wired to GPIO50/GPIO52/GPIO51. The
  root cause of slow/popping dual-deck playback after enabling the DAC was a
  PCM5102A I2S1 clock left at the BSP 44.1 kHz default while the first loaded
  track opened the shared ES8311 codec at 48 kHz. The audio output service now
  reconfigures PCM5102A to the loaded track sample rate before playback, and
  audio loader/decode/output tasks are pinned to CPU0 while LVGL remains on
  CPU1. A 60-second COM15 measurement with both decks playing reported
  `late=0 late_max=0 us`, stable ring fill, and stable decode timing.
  Follow-up hardware smoke on 2026-06-30 confirmed the module's RCA output and
  onboard 3.5 mm output both carry audio. The corresponding COM15 logs are
  `logs/p4_pcm5102a_boot_probe_20260630_123558.log` and
  `logs/p4_pcm5102a_rca_smoke_20260630_123632.log`; the runtime log showed
  `PCM5102A main out open @ 44100 Hz`, `late=0`, and no limiter activity for
  the single-deck RCA test window.

Validation note, 2026-06-14:

- `tests/run_p4_host_tests.ps1` is the primary Windows host regression runner
  when `make` is not available.
- `deck_core` no longer holds its state mutex while calling audio/UI APIs, and
  P4 UART RX coalesces high-rate JOG/PITCH events when the queue is full.
- `media_catalog_load_from_source()` and `media_catalog_get_from_source()` keep
  UI load workers pinned to the source captured at submit time. The previous
  JOINED library refresh UI path has since been removed from the MVP; remote
  library refresh remains a future re-enable item.
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
- Overview chrome polish on 2026-06-16 removes continuous LVGL title marquee
  invalidation, updates the remaining-time display in 50 ms buckets, and splits
  the blue-strip timer into fixed `-MM:SS` and `.CC` labels so the centiseconds
  no longer shift the seconds field. The same pass removes weak active-deck
  accent/border indicators, centers the beat-match dots and D2-vs-D1 phase
  meter around the main playhead, and slightly enlarges the Play/Cue touch
  buttons.
- P4 UI architecture refactor closed on 2026-06-13. Extracted modules include
  `ui_lvgl_backend`, `ui_overview`, `ui_library`, `ui_controls`,
  `ui_performance_tabs`, `ui_settings`, `ui_status`,
  `ui_overview_renderer`, `ui_overview_scheduler`, and frame-context helpers.
  `ui.c` owns init, screen registry, top-level tab switching, and frame context
  construction.
- Deck 2 lower Overview waveform jitter was fixed on 2026-06-13. The scheduler
  now  allow two-deck redraw budget when both decks are playing, but direct
  PPA overlay is allowed only for Deck 1. Deck 2 uses the normal LVGL
  invalidate/flush path, which visually removed the lower Deck 2 waveform
  jitter on hardware.
- **Overview waveform fix and visual polish (2026-06-16)**:
  - Fixed Deck 2 waveform stuttering (zapinjanje) and disappearing beatgrid lines during play state. The bug was traced to a fallback to `library_get_current_anlz()` inside `ui_deck_anlz` which caused cross-deck metadata corruption. Removed the fallback entirely; decks now strictly use their isolated and cloned snapshots in `s_deck_anlz_store`.
  - Redefined `s_overview_wave_rgb565_palette` and LVGL canvas palettes to dim the beatgrid lines: normal beats (index 3) are now dark cyan (`#1D5F5E`) and downbeats (index 8) are dark grey (`#5A5D64`). Playhead (index 4) remains bright white/grey for clarity.
  - Moved the lower deck waveform vertical position (`OVERVIEW_DECK2_WAVE_Y`) to **142**, resulting in a tight **1px** vertical gap between both decks' waveforms to create a "touching" alignment effect.
  - Relocated beat pulse indicators (flashing boxes) below the lower waveform (Y=288 for Deck 1 and Y=300 for Deck 2) to prevent overlapping with the shifted waveform.
  - Removed the redundant status indicator labels (`panel->label_status` showing "LOADED", "PLAY" etc.) below the DECK 1 and DECK 2 labels.
  - Resized the `panel->label_deck` ("DECK 1" / "DECK 2") to **76x38px** to match the dimensions of the Play/Cue buttons, reduced the font size to **16** (`lv_font_montserrat_16`), centered the text (with a top padding of **10px**), and replaced the white play-state outline with a neon green (`COL_GREEN`) PFL outline on a neutral background.
- **Time formatting and Web Control performance optimization (2026-06-17)**:
  - Removed centiseconds from time representation and applied `hh:mm:ss` format across the physical screen (Overview status, deck panels, and Performance tabs) and the mobile web controller interface.
  - Suppressed synchronous and blocking UART logging (`ESP_LOGI` to `ESP_LOGD`) for the status API (`/api/status`) and DNS/Captive Portal redirects, resolving main waveform micro-stuttering.
  - Applied CPU Core Affinity (Task Pinning): pinned the main `lvgl` graphics task to **Core 1** and the HTTP web server task to **Core 0** (with `config.core_id = 0`), isolating waveform drawing from network interrupts and web socket processing.
- **Dual-deck waveform and P4 build-performance pass (2026-06-25; revised
  2026-07-01)**:
  - The Overview scheduler originally gave two main waveform redraw budget
    tokens when both decks were playing. After the 2026-06-30 RCA/audio smoke
    confirmed audio stability but visible waveform stutter, the scheduler was
    briefly revised back to one main waveform redraw token per UI tick with
    alternating deck order. Follow-up hardware testing showed that visual
    stutter remained and the real failing case was mixed 44.1/48 kHz
    dual-deck playback. After the per-deck audio resampler fix, the scheduler
    again allows both playing deck waveforms to redraw in the same UI tick so
    each deck keeps full visual cadence.
  - P4 `sdkconfig.defaults` now selects performance optimization and disables
    LVGL examples/demos. If a local ignored `sdkconfig` already exists, it must
    be regenerated or aligned before flashing.
  - Switching to the performance build exposed latent `-O2 -Werror`
    string-truncation warnings. These were fixed in the library, media catalog,
    and UI cache-copy paths with explicit bounded-copy helpers rather than by
    suppressing compiler diagnostics.
  - Hardware smoke after the performance build exposed a `deck` task stack
    protection fault when Browse entered the Library UI. The root cause was the
    controller event path calling `ui_show_library -> ui_library_fill_row ->
    media_catalog_get_row` on the `deck` task stack. The stabilizing fix raises
    the `deck` task stack from 4096 to 8192 bytes. A future architectural cleanup
    should move controller-triggered UI navigation onto the LVGL/UI task context
    instead of doing table work on the deck-control task.
- **Splash screen port (2026-06-26)**:
  - Ported the P4 LVGL splash screen from `origin/codex/splash-screen` onto the
    current Phase 7 branch without merging the older branch state. The P4 UI now
    builds the main screen, shows a temporary black splash screen with
    `PajoNiiiR` rendered in `Musieer_80`, then returns to the already-built main
    screen after three seconds.
  - Added a static regression guard in `tests/splash_port/test_splash_port.ps1`
    so future branch merges do not accidentally drop the splash source, CMake
    entries, `ui.c` callback wiring, or the `ctrl_rx` stack increase.
  - Carried forward the `ctrl_rx` stack increase from 2048 to 4096 bytes from
    the splash branch. This is separate from the later `deck` task stack fix.

## Phase 7: Extended DDJ-FLX4 Control Surface

Status: input mapping and hardware inventory are substantially complete. The
MVP control path, Browse press, Play/Cue/PFL LED feedback, FLX4 reconnect LED
resynchronization, Smart CFX/Smart Fader input mapping, extended deck
transport inputs, mixer/monitoring analog inputs, pad-mode inputs, pad-action
ranges, and P4-driven VU output are implemented and software-tested. Hardware
smoke on 2026-06-21 verified the direct DDJ-FLX4 pad mode buttons (`HOT CUE`,
`PAD FX1`, `BEAT JUMP`, `SAMPLER`), shifted secondary pad modes, sampler,
beat-loop, key-shift, beat-jump, and loop-control input routing as documented
in `docs/DDJ_FLX4_MIDI_MAP.md`. P4 behavior for Loop In/Out, Reloop/Exit,
loop halve/double, normal/shifted Beat Loop pads, Beat Jump buttons/pads,
Tempo Range, Beat Sync BPM-match-on-press with paused-deck phase align, and
Hot Cue store/recall/clear is implemented. Three-band EQ DSP is implemented
per deck in the P4 audio path. Smart CFX now toggles P4-owned filter DSP from
the FLX4 filter knobs with a softened raw-to-effective macro curve, and Smart
Fader now toggles a conservative crossfader transition-assist curve. Beat FX section mapping and the first P4-owned state
model are implemented for effect select, beat size, target, depth, on/off, and
clear/reset. The Overview right-side Beat FX panel renders that P4-owned
snapshot directly, and `/api/status` exposes the same Beat FX snapshot for smoke
testing without serial log spam when a network transport is present. The HTTP
server and captive DNS remain disabled while the `wifi_link` shim returns
`ESP_ERR_NOT_SUPPORTED`; starting `esp_http_server` without an initialized
TCP/IP stack causes a boot-time lwIP assertion on P4. Beat FX FILTER now applies
a conservative target-aware low-pass DSP from the Beat FX depth control; Beat FX
ON/OFF LED feedback is P4-owned and hardware-smoke verified as of 2026-07-01.
Beat FX Echo/delay has a first fixed-delay DSP slice: P4 owns the delay lines,
deck_core routes the existing Echo state into the audio engine, depth controls
wet/feedback amount, and `/api/status.diagnostics.beat_fx_echo` exposes
allocation/enabled/delay telemetry. Current beat-size mapping intentionally uses
a fixed 120 BPM-style delay table (`1/4=125 ms`, `1/2=250 ms`, `1=500 ms`,
`2/4=1000 ms`); live BPM-synced delay calculation remains deferred. Hardware
smoke on 2026-07-01 confirmed FILTER and Echo audio behavior, gradual depth
response, CH1/CH2/1&2 target routing, and the ON/OFF LED following P4 state.
Pad FX now has a first P4-owned DSP slice behind synthetic/control-link
`CTRL_PAD_ACTION` events for PAD_FX1/PAD_FX2, using the existing filter and
delay primitives. Full FLX4 Pad FX physical pad input mapping is implemented
from the official Pioneer/AlphaTheta MIDI message PDF because the XML reference
does not expose a complete Pad FX pad range. Hardware smoke on 2026-07-01
confirmed Pad FX filter pads and Echo pad routing; short Echo presses now keep
a host-tested release tail instead of clearing the delay buffer immediately.
Normal Pad FX pad LED hardware smoke also passed on 2026-07-01.
Sampler, stem, and key-shift behavior remains deferred until
standalone P4 feature definitions exist.

Integration status as of 2026-06-26: the Phase 7 implementation branch and the
P4 splash-screen port are merged into `master`, host tests and both ESP-IDF
targets passed before the merge, and the merged `master` was pushed. Stale
completed Codex branches were removed locally and remotely. The only preserved
non-master branch is the old `codex/flx4-extended-controls` worktree, which is
dirty and contains experimental Smart/DSP changes that require a separate
review before reuse or deletion.

Goal: implement the remaining useful DDJ-FLX4 controls without importing
Mixxx runtime logic or moving authoritative state away from the P4.

Mapping policy:

- use `docs/reference/Pioneer-DDJ-FLX4.midi.xml` as the source for MIDI
  status, midino, message type, deck channel, shift channel, and 14-bit
  MSB/LSB pairing;
- Mixxx XML mapping is accepted as a fully verified, authoritative source for
  all remaining controls (due to 100% accuracy in all tests);
- do not execute or reproduce Mixxx JavaScript state logic on the S3. XML
  entries marked `Script-Binding` provide MIDI addresses only;
- keep the S3 limited to USB MIDI parsing, input normalization, coalescing,
  and semantic event forwarding. The P4 owns deck, mixer, pad mode, effect,
  playback, and LED state;
- preserve the existing `0xA5` frame and extend its semantic ID namespace
  unless a measured payload limitation requires a protocol version change.

Implementation order:

1. **Close MVP input gaps** ✅
   - map Browse press (`0x96/0x41`) to a Library/Overview toggle semantic event;
   - verify press/release handling and ensure Browse rotate remains relative;
   - add S3 mapper, control-link, and P4 browser-routing tests.
2. **MVP LED reconnect resynchronization** ✅
   - publish FLX4 USB connection state from S3 to P4;
   - force a P4-owned LED snapshot after reconnect;
   - verify Play/Cue/PFL LED recovery without changing playback or deck state.
3. **Smart CFX / Smart Fader raw input mapping** ✅
   - physically capture SMART CFX (`0x96/0x00`) and SMART FADER (`0x96/0x01`);
   - map each button as a momentary semantic press/release event;
   - P4 behavior now toggles Smart CFX/Smart Fader state, drives the verified
     Smart LEDs, exposes state through the mixer snapshot/status API, applies
     Smart CFX filter DSP from the channel filter knobs with raw/effective
     snapshot separation, and applies a safe Smart Fader transition-assist
     crossfader curve.
4. **Extended controller inventory** ✅
   - inventory created in `docs/DDJ_FLX4_MIDI_MAP.md` from 281 XML input
     controls and 112 XML output candidates;
   - grouped large pad-mode ranges so implementation can add compact semantic
     pad action events instead of hundreds of one-off IDs;
   - marked unsupported Mixxx-only behavior as deferred until standalone P4
     behavior exists.
5. **Deck modifiers and transport extensions**
   - add Shift, Beat Sync, tempo-range, vinyl, and other deck transport
     buttons that have a clear standalone P4 behavior;
   - send modifier press/release events to the P4 instead of keeping hidden
     playback state on the S3;
   - defer controls whose standalone behavior is not defined rather than
     copying a Mixxx script callback name as behavior.
   - first firmware slice maps Shift, Cue+Shift track-start, Beat Sync, and
     Beat Sync+Shift tempo-range IDs end to end. Cue+Shift has P4 behavior
     and seeks the addressed deck to 0 ms while pausing it. Beat Sync now
     toggles deck-local sync state and, when enabled, applies a one-shot BPM
     match to the other deck by setting effective pitch percent. Sync uses
     precise ANLZ `bpm_x100` when available and can exceed the selected Tempo
     Range up to the audio engine's internal ±20% safe clamp, because selected
     Tempo Range is a manual fader scale, not a sync accuracy limit. When the
     target deck is paused and both decks have
     beatgrids, it also seeks the target deck to the nearest beat whose
     `beat_phase` matches the reference deck's nearest beat. Phase-align seek
     is intentionally skipped while the target deck is playing because hardware
     logs showed a playing seek can drain the deck ring buffer and produce an
     output-late warning. This does not yet implement continuous following.
     Tempo Range cycles deck-local `±6%`, `±10%`, and
     `±16%` fader ranges and reapplies the current fader value to the audio
     engine.
   - second firmware slice expands the 7-byte control-link namespace to 32
     deck-local controls per deck, maps loop, beat-jump, pad mode/action, trim,
     EQ, filter, and headphone-mix semantic inputs, and adds P4-driven FLX4 VU
     meter output. These mappings are software-regression covered but remain
     hardware-capture pending unless individually marked verified in
     `docs/DDJ_FLX4_MIDI_MAP.md`.
   - S3 MIDI host robustness was verified on hardware on 2026-06-21 after a
     controller-responsiveness regression was traced to high-rate raw MIDI INFO
     logging and low-priority VU feedback filling the USB MIDI OUT queue. Raw
     packet logs are now DEBUG-only in translator mode, VU meter packets are
     dropped when the OUT queue has backlog, and Play/Pause stayed responsive
     with both decks playing.
   - final Phase 7 input smoke verified loop in/out, reloop/exit, loop
     halve/double, and beat-jump back/forward semantic routing on both decks;
   - Tempo Range behavior was hardware-smoked on 2026-06-25: Shift+Beat Sync
     cycles deck-local `±6%`, `±10%`, and `±16%` ranges and the tempo fader
     affects playback with the selected range;
   - P4 loop behavior is implemented for Loop In/Out, Reloop/Exit,
     halve/double, normal Beat Loop pads, and shifted momentary Beat Loop pads
     using the per-deck `audio_engine` loop API plus beatgrid/BPM duration
     calculation. Beat Jump behavior is implemented for shifted cue/loop call
     buttons and Beat Jump pads using beatgrid/BPM target calculation; Beat
     Jump pad hardware behavior smoke passed on both decks on 2026-07-01.
     Beat Sync BPM-match-on-press with paused-deck phase align and Tempo Range
     cycling are implemented; continuous sync following remains out of scope.
6. **Mixer and monitoring controls**
   - add trim, three-band EQ, filter, headphone mix, and other XML-exposed
     master controls using the XML 14-bit definitions;
   - add explicit P4 mixer parameters, clamping, snapshots, persistence only
     where already consistent with settings ownership, and LED/state feedback
     where the controller exposes it;
   - coalesce high-rate analog events using the existing latest-value policy.
   - three-band EQ is implemented for Deck 1 and Deck 2: S3 forwards the
     verified 14-bit FLX4 EQ controls, P4 stores raw per-band state in the
     mixer snapshot, and `audio_output_mixer` applies the deck-local EQ before
     channel fader/crossfader summing. Center raw `8192` is unity, minimum is
     band kill, and maximum is a conservative `+6 dB` boost.
7. **Performance pads and pad modes**
   - inventory the four direct physical pad mode buttons (`HOT CUE`, `PAD FX1`,
     `BEAT JUMP`, `SAMPLER`) plus shifted secondary modes (`Keyboard/Stems`,
     `Pad FX2`, `Beat Loop`, `Key Shift`) and their MIDI ranges from the XML;
   - connect P4-owned semantic pad-mode inputs for Hot Cue, Beat Loop,
     Beat Jump, Key Shift, Keyboard/Stems, Pad FX1, Pad FX2, and Sampler;
   - represent pad mode and pad action as separate P4-owned semantic state;
   - Hot Cue pad behavior is implemented in P4: pad 1-8 stores the current
     deck position into an empty per-track slot or recalls an existing slot via
     `audio_engine_deck_seek()`, while shifted Hot Cue pads clear the matching
     slot. Deck 1 set/recall/shift-clear hardware smoke passed on
     2026-06-21; Deck 2 behavior uses the same deck-local implementation and
     remains pending for hardware smoke;
   - hardware smoke verified sampler pads 1-8 on both decks, key-shift pads
     1-8 on both decks, beat-loop pads 1-8 on both decks, and most beat-jump
     pads. Normal and shifted Beat Loop pad behavior plus Beat Jump pad
     behavior is implemented in P4 using beatgrid/BPM calculation and remains
     pending for hardware behavior smoke. Pad FX has a first host-tested P4 DSP
     slice and physical input mapping from the official MIDI message PDF for
     PAD_FX1/PAD_FX2 pad actions; hardware smoke passed on 2026-07-01 for pad
     behavior, Echo tail, and normal Pad FX pad LEDs. Sampler, stem, and
     key-shift behavior remains deferred until standalone P4 feature
     definitions exist.
8. **Effects controls**
   - map only controls backed by a defined P4 effect engine and parameter
     model;
   - keep unsupported Mixxx QuickEffect/BeatFX bindings documented but do not
     expose no-op controls as completed functionality.
9. **LED feedback expansion**
   - derive candidate output status/midino values from the XML output section;
   - drive LEDs only from P4-confirmed state through the existing S3 MIDI Out
     queue;
   - first slice implemented in firmware: P4-owned per-deck pad mode LED
     snapshot and S3 XML-derived MIDI OUT translation for Hot Cue, Keyboard,
     Pad FX1, Pad FX2, Beat Jump, Beat Loop, Sampler, and Key Shift;
   - Beat Loop pad LED output is implemented for the normal pad LED notes and
     derived from P4-owned active Beat Loop pad state plus selected Beat Loop
     pad mode; shifted mirror LED notes remain deferred. A 2026-07-01 fix
     removed the previous 120-BPM duration-inference dependency, and hardware
     LED smoke passed on both decks;
   - Beat Sync LED feedback is implemented as a P4-owned per-deck sync-enabled
     state (`deck_core.sync_enabled`) and XML-derived S3 MIDI OUT note `0x58`;
   - Loop In/Out LED feedback is implemented from P4-owned loop marker/loop
     state; Loop In lights immediately after the pending marker is set, and
     active loops light both Loop In and Loop Out LEDs for that deck;
   - Beat FX ON/OFF LED feedback is implemented from P4-owned Beat FX enabled
     state and clear/reset forces it off;
   - pad-mode, Beat Sync, Loop In/Out, and Beat FX ON/OFF LEDs have hardware
     smoke coverage as recorded in `docs/validation/FLX4_LED_MIDI_OUT_CAPTURE.md`;
     extended reconnect resynchronization is implemented for FLX4 USB replug,
     S3 reset, and P4 reboot recovery through the S3 heartbeat connected-state
     refresh; P4-reset hardware smoke passed on 2026-06-26.

Required artifacts:

- extend `docs/DDJ_FLX4_MIDI_MAP.md` with an inventory table containing
  physical control, XML status/midino, message encoding, semantic ID, P4 owner,
  implementation status, and hardware verification status;
- add constants and parser cases in
  `firmware/control-board-s3/components/flx4_midi_host/`;
- extend matching IDs in both S3 and P4 `control_link.h` files;
- add P4 routing/state behavior only in the component that owns the feature;
- extend `tests/flx4_midi_host`, `tests/control_link_protocol`, and the
  relevant P4 host test suite before each control group is enabled.

Validation per control group:

- S3 host tests pass, including press/release, deck/shift channel separation,
  relative encoder direction, and 14-bit MSB/LSB assembly;
- P4 host tests pass for semantic routing and authoritative state changes;
- both ESP-IDF targets build when the shared protocol changes;
- physical FLX4 smoke testing is treated as a final acceptance step to verify
  standalone behaviors, not as a development blocker;
- unsupported or deferred controls remain explicitly marked and do not
  silently emit a different action.

Exit criteria:

- every implemented physical control has a documented XML-derived or raw-captured mapping,
  semantic event, P4 owner, automated test, and hardware acceptance result;
- Browse press, Smart CFX, Smart Fader, and all selected control groups work
  end to end from FLX4 to S3, through `0xA5`, to P4 behavior;
- S3 contains no authoritative playback, mixer, pad-mode, or effect state;
- LEDs recover to P4-confirmed state after S3 or FLX4 reconnect;
- documentation distinguishes implemented, deferred, and unsupported Mixxx
  mappings.
