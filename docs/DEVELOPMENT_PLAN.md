# Development Plan

Status: current phase ledger, P4-only branch reconciled 2026-09-01.

> On `feat/p4-dual-usb-host`, P4 directly owns USB0 storage and USB1 controller
> MIDI/audio. The S3 UART/heartbeat/debug AP, profile-transfer, monitor PCM and
> firmware/release paths have been removed from the active build. Older phase
> entries below remain dated evidence for the dual-processor `master` baseline;
> they are not instructions for this branch. Physical VBUS qualification,
> direct-controller acceptance and the combined soak remain open.

The 2026-08-27 cleanup also removed the historical S3 firmware tree and its
dedicated test runners. The direct P4 controller path now gates the built-in
FLX4 mapper by exact USB identity, retries held-state delivery until the deck
queue accepts it, resolves SD profiles outside the USB client task, validates
connection epochs before activation, publishes the LED sink under a critical
section and retries controller bootstrap while exposing health diagnostics.

## Executive status

| Capability | State |
| --- | --- |
| FLX4 host, semantic controls and LEDs | Implemented and hardware-smoked |
| Dual-deck playback, mixer and UI | Implemented and hardware-smoked; DSI-synchronised dual-waveform cadence accepted in focused P4 smoke 2026-07-17 |
| MAIN + USB headphone cue audio | Implemented and hardware-smoked |
| Vinyl/scratch | Remediation complete; dual-deck hardware validation passed 2026-07-11 |
| Master Tempo/key lock | Implemented; basic hardware behavior accepted 2026-07-12; deterministic five-minute simultaneous dual-deck host soak passed 2026-07-26, while long P4 CPU/listening acceptance remains |
| End-of-track drain/replay | R1 implemented and basic hardware acceptance passed 2026-07-13 |
| Beat FX | Filter/Echo/Flanger/Delay all hardware-accepted 2026-07-24; headroom soft-clip added in `RC1-223-gdfa619a9` |
| Idle screensaver | Implemented and hardware-accepted 2026-07-24 in `RC1-237-g7bf0fd3c`. Fixed two-minute timeout by operator decision; the Settings entry from the plan was declined, not skipped |
| Loop (manual in/out + beat pads) | Timing corrected and hardware-accepted; armed Loop In dynamic overlay burning implemented in `ui_overview.c` for smooth 60 FPS scrolling highlight without strip invalidation. Verified on P4 hardware 2026-08-19 |
| Controller profiles | Firmware path implemented; FLX4 profile hardware-verified and deployed in `RC1-131-gc391e306`; `generic_midi_ci` and a specification-derived Hercules Inpulse 500 profile are compiler/registry/runtime/LED host-tested, with Hercules P4 Sync Off/autoloop behavior covered; non-FLX4 hardware and remote update acceptance pending |
| Direct-controller runtime hardening | Identity-gated built-in mapping, durable held-state retries, off-USB-task profile activation with epoch validation, truthful UAC capability reporting, retrying bootstrap and bounded transfer/UAC fault recovery implemented. Host suite and clean ESP-IDF 6.0.2 P4 build pass; exact `RC2-109-g269036b` OTA, one USB1 reconnect and post-reconnect dual-deck audio pass. Repeated reconnect/profile acceptance remains pending |
| P4 OTA and rollback | Signed negative-path/rollback acceptance passed 2026-07-14; P4 RC2 application OTA and full IDF 6.0.2 boot-chain flash passed. S3 OTA evidence is retained as historical baseline only |
| Pull OTA (P4, Wi-Fi STA) | **Core path proven end to end on hardware 2026-07-24.** Software hardening now enforces monotonic newer-only pull offers, a ten-minute offer lifetime, channel size/SHA-256 verification, strict relative bundle paths, canonical `pajoniiir.local` mDNS and a dynamic AP-IP/mDNS Host allow-list. Hardware re-smoke of the hardened path remains |
| ANLZ metadata loading | Unified single-resolver path implemented, host-tested and deployed; on-device timings 31 ms warm / 267 ms warm-under-load / 698 ms cold |
| microSD service journal | Structured event log with rotation, status and `GET /api/diagnostic-log` implemented and hardware-verified 2026-07-21 |
| Master-output recorder | **Compiled out by default since 2026-07-24** (`CONFIG_AUDIO_RECORDER_ENABLED`, off). Implemented and functionally accepted 2026-07-21, but write latency is card-bound, not firmware-bound; shelved rather than removed. Safety hardened: producer stop-gate, transactional finalise (`patch`→`sync`→`close`→`publish`) and durability-failure propagation |
| Bounded compressed cache | On `master` since the ESP-IDF 6.0.2 merge. MP3/WAV/FLAC use a seekable LRU page cache (8 × 32 KiB per deck) instead of whole-file PSRAM; eliminates `TRACK TOO LARGE` and fragmentation. Focused real-MP3 playback passed 2026-08-02. WAV/FLAC were not exercised because the audited USB contained 68 MP3 files but zero physical WAV/FLAC files despite stale PDB entries; sustained dual-deck acceptance remains pending |
| Paginated Library UI | On `master` since the ESP-IDF 6.0.2 merge. LVGL table renders one 8-row page with PREV/NEXT (≤40 live cells instead of up to 5120). Host-tested and operator-confirmed on P4 hardware 2026-08-02 |
| Immutable track sort | On `master` since the ESP-IDF 6.0.2 merge. Library sorting uses double-buffered `uint16_t` row-order over immutable records. No large-struct copies or qsort. Software-tested |

The latest fully functionally accepted hardware baseline remains
`RC1-123-g587cd7a1`. Both boards successfully installed and reported the RC2
application through OTA on 2026-08-02. That OTA did not update the boot chain:
the first captured P4 RC2 boot still used an ESP-IDF 5.5 bootloader. P4 was then
fully wired-flashed with `RC2-3-g136aad7`; its ESP-IDF v6.0.2 bootloader and
microSD mount now pass. S3 was then full-flashed with the exact clean RC2
bootloader/application pair; its image metadata confirms IDF v6.0.2 and its P4
control-link report confirms `RC2`, `ota_0`, `VALID`. A later focused smoke
passed P4 display/touch/Settings and paginated Library, FLX4 MIDI/LED,
PCM5102A MAIN, FLX4 CUE/MONITOR and real MP3 playback. WAV and FLAC remain
untested because no physical fixtures were present on the audited Rekordbox
USB. The broader recovery, sustained-load and fault-injection matrix remains
pending. See `validation/P4_IDF6_SDMMC_SMOKE_20260802.md`,
`validation/S3_IDF6_WIRED_FLASH_20260802.md` and
`validation/RC2_FOCUSED_FUNCTIONAL_SMOKE_20260802.md`.

The `migration/esp-idf-6.0.2` branch is **merged into `master`** and deleted;
`master` builds only under ESP-IDF 6.0.2. The release prefix is now **`RC2`**
(annotated tag on `56905c89`), and a clean dual-target `RC2` build was recorded
on 2026-07-30 in `validation/CLEAN_RELEASE_RC2_BUILD.md`. The clean RC2 release
record remains compilation/package evidence; focused P4 boot/microSD evidence
is recorded separately and does not close the other hardware rows.
Next acceptance work is the remaining ESP-IDF 6.0.2 hardware validation matrix
(starting with correctly exported WAV/FLAC fixtures and sustained USB/cache
instrumentation), the
remaining targeted Phase 20/E1A and remote controller-profile matrix, followed
by production key provisioning/rotation, enclosure power/thermal/RF soak,
longer dual-deck key-lock P4 CPU/listening testing, selected pending MIDI
hardware rows and physical Hercules Inpulse 500 profile acceptance. Historical phase text
below is retained as the implementation record.

## Phase 0: Baseline Import And Documentation

Status: complete.

Deliverables:

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
  The cache diagnostics report `FULL`, `OFFSET`, `EDGE`, and `NONE` updates plus
  total rendered columns and blits.
- The 2026-07-17 COM15 investigation found the remaining rare full-screen flash
  below the renderer logic: the original approximately 60.48 Hz full-cadence
  path produced three user-visible flashes paired one-for-one with DSI
  `can't fetch data from external memory fast enough` underruns. Both RGB565
  waveform strips are PSRAM-backed. One-deck-per-tick variants reduced memory
  pressure but produced the operator's watery motion, and a DW-GDMA QoS trial
  still produced two flashes, so neither workaround was retained. Firmware now
  extends vertical front porch for a 49.981 Hz panel refresh and runs
  `ui_update()` from coalesced `on_refresh_done` notifications on the LVGL task.
  The full two-deck redraw budget remains intact. A focused approximately
  132-second dual-play smoke ended with zero DSI underruns, zero monitor-PCM
  drops and no panic/reset; the operator confirmed fluid motion and no flash.
- Mixed 44.1/48 kHz dual-deck playback was fixed on 2026-07-01. Hardware
  diagnosis showed the OK pair France Gall + Comanchero were both 44.1 kHz,
  while the failing Men At Work + Caribbean Blue pair mixed 44.1 kHz and
  48 kHz. The output mixer now folds `source_sample_rate / output_sample_rate`
  into each deck's effective resampler step instead of using `pitch_factor`
  alone. Host regression coverage asserts that a 48 kHz deck on a 44.1 kHz
  output consumes 160 source frames per 147 output frames. Hardware smoke after
  the fix confirmed audio OK, fluid waveform, normal Caribbean Blue playback,
  and no reboot.
- Before adding PCM5102A hardware support, the readiness steps were to create
  a P4 pinout inventory, fix the output sample-rate strategy, split logical
  `master_out[]`/`hp_out[]` buffers, harden BSP audio init, and only then select
  approved P4 GPIOs for the PCM5102A.
- The concrete DAC bring-up sequence was to
  use GPIO50/GPIO52/GPIO51 as the bench-verification candidate set, reject the
  GPIO22/GPIO23/GPIO24/GPIO25 proposal because GPIO23 is LCD backlight PWM, and
  route PCM5102A as MAIN OUT while keeping ES8311 as monitor/headphones/speaker.
- **Superseded for the product build (2026-07-02):** the FLX4 USB headphones
  are the CUE/MONITOR output, so ES8311 is dropped there. Final topology:
  PCM5102A RCA = MAIN OUT (I2S unit 1), FLX4 USB = cue (monitor link on unit 0),
  ES8311 disabled to free unit 0 (only 2 usable I2S units on eco2 P4). See
  `docs/validation/FLX4_USB_AUDIO_E2E_SMOKE.md` (hardware-validated).

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
- Firmware source/cache/progress state is a host-tested `audio_fw_preload`
  slot (legacy type name), with one bounded cache slot allocated per deck.
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
  and USB removal now holds the global audio LOAD barrier across stop and
  library/deck-state cleanup.
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

Goal: adapt the P4 display from single-deck CDJ UI to Pajoniiir dual-deck status.

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
  tabs expose utility/status strips, the retired Key Shift screen has been
  removed from the local P4 UI, Settings uses compact audio/wireless/status
  sections, and Overview owns per-deck cue marker objects for D1 and D2.
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
- Browse rotate now controls a shared coarse zoom level for both main Overview
  waveforms when the Overview tab is active. The steps are 4, 8, 12, 16, and
  24 beats; the default remains 16 beats. Browse rotate still navigates the
  Library table when the Library tab is active and is ignored on other tabs.
- Follow-up polish enlarges the main waveform canvas and maps Rekordbox waveform
  color hints plus amplitude fallback into a Pioneered-like palette: hot pink,
  blue, cyan, green, amber, purple, and white downbeat/grid markers.
- Beat-grid clarity pass separates grid colors from waveform colors: regular
  beat-match guides are full-height dim background lines drawn behind the
  waveform with a small red cap, while downbeats remain foreground reference
  markers. Deck 1 draws the regular beat cap at the bottom and Deck 2 at the
  top, making the paired beat-match guides easier to read. This keeps
  beat-match alignment visible without bright lines shimmering across waveform
  transients on wider zoom levels.
- Loading a track from Library no longer direct-renders the large main waveform
  from the load path. The cache update is deferred to the regular Overview
  scheduler, which can render and blit the RGB565 strip in the same UI path.
  After any track load, both deck overlays are armed for a short reblit window
  so LVGL flushes from one deck cannot leave the other deck's direct overlay
  blank. Hardware smoke on 2026-07-01 confirmed that loading Deck 1 shows its
  main waveform without touching the screen, loading Deck 2 keeps both main
  waveforms visible, and later playback/zoom redraws continue normally.
- Overview now uses a compact two-row beat phase strip below the lower waveform.
  The strip is centered from the overview playhead/waveform geometry and uses
  the same interpolated deck position as the waveform, while the old signed
  D2-vs-D1 phase meter code is no longer part of the runtime UI.
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
  no longer shift the seconds field. Later overview polish restores a restrained
  active-deck badge, centers the beat-match dots around the main playhead from
  waveform geometry, and keeps Play/Cue touch controls compact.
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
  - Redefined `s_overview_wave_rgb565_palette` and LVGL canvas palettes for
    distinct waveform/grid colors. Later waveform-zoom polish draws regular
    beat-grid guides as dim background lines with red top caps behind the
    waveform to avoid shimmer on wider zoom levels; playhead/downbeat reference
    markers remain high contrast in the foreground.
  - Moved the lower deck waveform vertical position (`OVERVIEW_DECK2_WAVE_Y`) to **142**, resulting in a tight **1px** vertical gap between both decks' waveforms to create a "touching" alignment effect.
  - Relocated beat pulse indicators (flashing boxes) below the lower waveform.
    Later polish derives both rows from the lower waveform bottom and the
    overview playhead center instead of fixed pixel positions.
  - Removed the redundant status indicator labels (`panel->label_status` showing "LOADED", "PLAY" etc.) below the deck labels.
  - Later UI polish replaced the larger `DECK 1` / `DECK 2` labels with compact `D1` / `D2` badges, separated the VU meters from the Play/Cue touch targets, and added static layout guards so the VU channel cannot overlap the transport buttons or deck badges.
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
    media_catalog_get_row` on the `deck` task stack. The stabilizing fix raised
    the `deck` task stack from 4096 to 8192 bytes. The architectural cleanup was
    completed on 2026-07-26: controller-triggered browse/load work is queued and
    drained by `ui_update()` on the LVGL task, with at most eight commands per
    frame.
- **Splash screen port (2026-06-26)**:
  - Ported the P4 LVGL splash screen from `origin/codex/splash-screen` onto the
    current Phase 7 branch without merging the older branch state. The P4 UI now
    builds the main screen, shows a temporary black splash screen with
    `Pajoniiir` rendered in `Musieer_80`, then returns to the already-built main
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
Tempo Range, Beat Sync BPM-match-on-press with one-shot phase align while
playing, Sync Master selection, Browse+Shift accelerated navigation,
Reloop/Exit+Shift loop stop/forget, Loop Adjust In/Out, Quantize for loop
boundaries, slip-censor MVP, and Hot Cue store/recall/clear are implemented.
Trim/pregain and
three-band EQ DSP
are implemented per deck in the P4 audio path. Smart CFX now toggles P4-owned
filter DSP from the FLX4 filter knobs with a softened raw-to-effective macro
curve and hardware-smoked HI/LOW behavior; Smart Fader now toggles a
conservative crossfader transition-assist curve with hardware smoke passed on
2026-07-01.
Beat FX section mapping and the first P4-owned state
model are implemented for effect select, beat size, target, depth, on/off, and
clear/reset. The Overview Beat FX rail renders that P4-owned snapshot directly,
and `/api/status` exposes the same Beat FX snapshot for smoke testing without
serial log spam when Wi-Fi Remote is enabled. HTTP server and captive DNS
startup remain gated behind successful ESP-Hosted Wi-Fi/AP init because starting
`esp_http_server` without an initialized TCP/IP stack causes a boot-time lwIP
assertion on P4. Beat FX FILTER now applies
a conservative target-aware low-pass DSP from the Beat FX depth control; Beat FX
ON/OFF LED feedback is P4-owned and hardware-smoke verified as of 2026-07-01.
Beat FX Echo has a beat-time DSP slice: P4 owns the delay lines, deck_core
routes the Echo state into the audio engine, depth controls wet/feedback amount,
and `/api/status.diagnostics.beat_fx_echo` exposes allocation/enabled/delay
telemetry. Beat-size mapping derives delay time from
the target deck effective BPM whenever Beat FX state is applied, falls back to
120 BPM when BPM is unavailable, and caps delay at 1000 ms to match the current
delay-line budget. Later tempo, Beat Sync or track-load changes do not
automatically retime an already active time effect. Hardware
smoke on 2026-07-01 confirmed FILTER and Echo audio behavior, gradual depth
response, CH1/CH2/1&2 target routing, beat-derived Echo beat-size changes, and
the ON/OFF LED following P4 state.

**2026-07-10 Beat FX DSP + rail update.** The one-knob channel filter (and the
shared Beat FX filter, both in `audio_filter.c`) became a resonant ZDF
state-variable filter with an exponential low-pass/high-pass sweep to full kill;
the Echo (`audio_delay_fx.c`) gained per-generation feedback damping and a ~2 s
ring-out tail on switch-off; Smart CFX moved from the softened macro to a
smoothstep response curve; and a beat-derived **Flanger** (`audio_flanger_fx.c`)
was added as a third Beat FX effect, so the deck_core cycle is now
FILTER → ECHO → FLANGER. The Overview Beat FX rail was redesigned as an
effect-colour-coded strip (Filter blue / Echo amber / Flanger magenta) with a
vertical depth meter. Host suites pass, and the FLX4 USB-headphones audio
profile was made the default build on both boards (folded into
`sdkconfig.defaults`; a plain `idf.py build` now has sound).

**2026-07-16 Beat FX Delay update.** A distinct beat-sized **DELAY** was
added as numeric effect value `4`; the existing `NONE=0`, `FILTER=1`, `ECHO=2`,
and `FLANGER=3` values remain stable. DELAY is a full-band one-shot repeat with
zero feedback, and Level/Depth controls its wet gain. ECHO remains the damped
multi-repeat feedback mode. Both modes share the existing per-deck stereo
`audio_delay_fx` line, so DELAY requires no additional PSRAM allocation; a live
ECHO/DELAY mode change resets that shared line so stale repeats cannot cross
between effect semantics. Delay time is sampled from effective BPM when Beat FX
state is applied, uses only the valid 40–300 BPM range, otherwise falls back to
120 BPM, and is capped at 1000 ms. It does not automatically follow a later
tempo, Beat Sync or track-load change until another Beat FX event republishes
the state.
The combined `1&2` target currently uses Deck 1 BPM for the shared delay time.
The selector is explicit rather than enum-modulo:
`FILTER → ECHO → FLANGER → DELAY → FILTER`, with Previous traversing the exact
reverse. `NONE=0` remains a compatibility sentinel, not a selectable effect or
reset command. CLEAR restores disabled FILTER, beat size 1, target `1&2` and
depth 64. The S3 continues to send the existing semantic Next/Previous events
over the unchanged `0xA5` wire protocol. P4 host tests plus `idf.py build` are
the software acceptance path; focused FLANGER and DELAY audio, target-routing,
beat-size and Level/Depth hardware smoke is still **PENDING**.

Pad FX now has a first P4-owned DSP slice behind synthetic/control-link
`CTRL_PAD_ACTION` events for PAD_FX1/PAD_FX2, using the existing filter and
delay primitives. Full FLX4 Pad FX physical pad input mapping is implemented
from the official Pioneer/AlphaTheta MIDI message PDF because the XML reference
does not expose a complete Pad FX pad range. Hardware smoke on 2026-07-01
confirmed Pad FX filter pads and Echo pad routing; short Echo presses now keep
a host-tested release tail instead of clearing the delay buffer immediately.
Normal Pad FX pad LED hardware smoke also passed on 2026-07-01.
Sampler, stem/Keyboard, and Key Shift behavior is out of product scope as of
2026-07-07. S3 ignores those FLX4 input ranges, P4 ignores stale/manual
control-link events for those modes, and unsupported mode LEDs remain OFF while
their numeric IDs stay reserved for compatibility.

Integration snapshot from 2026-06-26 through 2026-07-03: the Phase 7
implementation branch and P4 splash-screen port were merged into `master`,
verified, and pushed. The then-stale completed branches, including
`codex/flx4-extended-controls`, were reviewed and removed after confirming their
verified slices were already salvaged. That is a dated cleanup record, not a
permanent branch invariant. A 2026-07-20 branch audit then retired the last
outstanding branch: `codex/phase-8-implementation` was confirmed superseded by
`master` (which already carries its WAV/FLAC decode and S3 status-LED work), its
only unique content — the GPIO48 WS2812 RGB status-LED policy engine — was
archived under the annotated tag `attic/phase-8-status-led-policy`, and its
branch plus linked worktree were removed. All merged `codex/*` and `feature/*`
branches were then pruned locally and on `origin`, leaving only `master`. The
repository also moved to `https://github.com/dvucinozd/Pajoniiir.git` (the old
`ESP32-DDJ-FLX4` URL redirects). A 2026-07-26 follow-up audit found five
temporary remote maintenance branches and two stale local tracking branches.
All seven tips were ancestors of `master` with 0 unique commits, so they were
deleted locally/remotely and the inventory returned to only `master`. Re-run
`git branch --no-merged master`, count `master..<branch>` commits and archive
unique work under `attic/*` before deleting any future branch.

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
     snapshot separation and hardware-smoked HI/LOW sweep behavior, and applies
     a safe Smart Fader transition-assist crossfader curve with hardware smoke
     passed on 2026-07-01.
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
     Tempo Range is a manual fader scale, not a sync accuracy limit. When both
     decks have beatgrids, it seeks the target deck to a matching beat while
     preserving the reference deck's signed offset inside that beat, rather
     than only matching the nearest `beat_phase`. A 2026-07-01 hardware smoke
     pass confirmed the one-shot phase-align seek while the target deck is
     playing, and the Overview beat-match guide lines align after Beat Sync.
     This does not yet implement continuous following.
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
     Beat Sync BPM-match-on-press with signed intra-beat phase align and Tempo
     Range cycling are implemented; Sync Master selection is implemented from
     the shifted/long-press XML control. Continuous sync following remains out
     of scope.
   - Browse+Shift rotate/press, Reloop/Exit+Shift stop, Loop Adjust In/Out,
     Quantize, and Play+Shift slip-censor MVP are implemented in firmware from
     the Mixxx XML mapping. Host tests cover S3 mapping and P4 behavior;
     hardware smoke remains pending for this shifted control group.
   - Shifted Jog Search is implemented from the XML mapping as a deck-local
     relative seek at 1000 ms per encoder step. Jog Search touch/highspeed is
     mapped as a semantic input but does not yet alter the seek multiplier.
     Host tests cover mapping and P4 seek behavior; hardware smoke passed on
     2026-07-02.
6. **Mixer and monitoring controls**
   - add trim, three-band EQ, filter, headphone mix, and other XML-exposed
     master controls using the XML 14-bit definitions;
   - add explicit P4 mixer parameters, clamping, snapshots, persistence only
     where already consistent with settings ownership, and LED/state feedback
     where the controller exposes it;
   - coalesce high-rate analog events using the existing latest-value policy.
   - S3 now keeps a known-value snapshot for absolute mixer/monitor/effect
     controls and replays it to P4 after heartbeat-driven FLX4 connection
     refresh. This covers channel faders, crossfader, trim, EQ, filters,
     Master Level, Headphones Mix, and Beat FX Level/Depth, but deliberately
     excludes tempo faders and buttons/toggles.
   - trim/pregain is implemented for Deck 1 and Deck 2: S3 forwards the
     verified 14-bit FLX4 Trim controls, P4 stores raw per-deck pregain state
     in the mixer snapshot and `/api/status`, and output gain applies a bounded
     pregain scalar before the existing post-sum limiter. Center raw `8192` is
     unity, minimum is `0.25x`, and maximum is a conservative `+6 dB` boost.
     Hardware smoke on 2026-07-01 confirmed the full Trim travel after the
     output mixer clamp was widened to pass pregain boost into the master
     limiter.
   - master level is implemented from the official FLX4 MIDI PDF (`B6 08/28`):
     S3 maps the 14-bit control to `CTRL_ID_MASTER_VOLUME`, P4 stores it as
     runtime non-persistent master volume, exposes it in `/api/status`, and
     multiplies it with the persistent Settings master trim in the output gain
     path. Hardware smoke passed on 2026-07-01.
   - headphones mix is implemented for the P4 monitor path: S3 forwards the
     14-bit FLX4 Headphones Mix control (`B6 0C/2C`), P4 stores the raw value
     in the mixer snapshot and `/api/status`, and the P4 `hp_out` monitor bus
     blends cue/PFL with stereo master (`0` = cue/PFL, `16383` = master). In
     the product topology this bus is streamed over the P4-to-S3 I2S monitor
     link and then to the original FLX4 headphone jack via USB Audio Class.
     XIAO ESP32S3/Sense hardware smoke with audible FLX4 headphones passed on
     2026-07-07, including Headphones Mix and Headphones Level operator
     acceptance.
   - physical MASTER CUE is implemented from the official MIDI list
     (`96 63`, shifted `96 78`) as a P4-owned monitor master-cue gate.
     The official `96 68` message is Shift+Load Deck 1, not Master Cue.
     The main/RCA master output is unchanged; only the monitor/headphone master
     contribution is toggled. `LED_MASTER_CUE` is included in the reconnect-safe
     LED snapshot. Host tests cover input mapping, DSP gating, deck_core
     routing, and LED output; hardware smoke passed on 2026-07-02.
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
     Beat Jump, Pad FX1, and Pad FX2; Keyboard/Stems, Sampler, and Key Shift
     are documented XML addresses only and are ignored by the product firmware;
   - represent pad mode and pad action as separate P4-owned semantic state;
   - Hot Cue pad behavior is implemented in P4: pad 1-8 stores the current
     deck position into an empty per-track slot or recalls an existing slot via
     `audio_engine_deck_seek()`, while shifted Hot Cue pads clear the matching
     slot. Deck 1 set/recall/shift-clear hardware smoke passed on
     2026-06-21; Deck 2 behavior uses the same deck-local implementation and
     remains pending for hardware smoke;
   - previous hardware smoke identified sampler pad and key-shift pad MIDI
     ranges, but those controls are now out of product scope. Normal and
     shifted Beat Loop pad behavior plus Beat Jump pad
     behavior is implemented in P4 using beatgrid/BPM calculation and remains
     pending for hardware behavior smoke. Pad FX has a first host-tested P4 DSP
     slice and physical input mapping from the official MIDI message PDF for
     PAD_FX1/PAD_FX2 pad actions; hardware smoke passed on 2026-07-01 for pad
     behavior, Echo tail, and normal Pad FX pad LEDs. Sampler, stem/Keyboard,
     and Key Shift behavior was removed from the product scope on 2026-07-07.
8. **Effects controls**
   - map only controls backed by a defined P4 effect engine and parameter
     model;
   - Beat FX DELAY is implemented on P4 as value `4`: a beat-sized,
     full-band one-shot repeat with wet gain controlled by Level/Depth. It
     shares the Echo delay line without additional PSRAM, samples effective BPM
     when Beat FX state is applied, uses only 40–300 BPM (otherwise 120 BPM),
     caps the delay at 1000 ms, and uses Deck 1 BPM for target `1&2`. Later
     tempo/track changes do not automatically retime it; FLANGER/DELAY hardware
     smoke remains pending;
   - keep unsupported Mixxx QuickEffect/BeatFX bindings documented but do not
     expose no-op controls as completed functionality.
9. **LED feedback expansion**
   - derive candidate output status/midino values from the XML output section;
   - drive LEDs only from P4-confirmed state through the existing S3 MIDI Out
     queue;
   - first slice implemented in firmware: P4-owned per-deck pad mode LED
     snapshot and S3 XML-derived MIDI OUT translation for supported Hot Cue,
     Pad FX1, Pad FX2, Beat Jump, and Beat Loop mode LEDs. Keyboard/Stems,
     Sampler, and Key Shift LED IDs are retained only so reconnect/force paths
     can keep them OFF. Post-flash hardware smoke on 2026-07-07 confirmed
     supported modes still work and unsupported modes stay inert;
   - Beat Loop pad LED output is implemented for the normal pad LED notes and
     derived from P4-owned active Beat Loop pad state plus selected Beat Loop
     pad mode; shifted mirror LED notes remain deferred. A 2026-07-01 fix
     removed the previous 120-BPM duration-inference dependency, and hardware
     LED smoke passed on both decks;
   - Beat Jump normal pad LED output is implemented for the normal pad LED
     notes and derived from P4-owned loaded-track state while Beat Jump mode is
     selected. Shifted helper LED 7/8 output is implemented from the same
     loaded-track/mode state gated by held deck Shift; broader shifted mirror
     LED notes remain deferred. Post-flash FLX4 smoke passed on 2026-07-07:
     all 8 normal Beat Jump pads lit in Beat Jump mode with a loaded track,
     and shifted helper LED 7/8 lit while deck Shift was held;
   - Beat Sync LED feedback is implemented as a P4-owned per-deck sync-enabled
     state (`deck_core.sync_enabled`) and XML-derived S3 MIDI OUT note `0x58`;
   - Loop In/Out LED feedback is implemented from P4-owned loop marker/loop
     state; Loop In lights immediately after the pending marker is set, and
     active loops light both Loop In and Loop Out LEDs for that deck;
   - Beat FX ON/OFF LED feedback is implemented from P4-owned Beat FX enabled
     state and clear/reset forces it off;
   - Master Cue LED feedback is implemented from P4-owned monitor state and is
     included in reconnect snapshots; hardware smoke passed on 2026-07-02;
   - the 2026-07-07 LED batch smoke passed for Censor, Cue+Shift / track-start,
     Loop Adjust In, Loop Adjust Out, Track Load Deck 1/2, and post-removal pad
     mode LED behavior;
   - Cue+Shift / track-start and Loop Adjust In/Out now emit P4-owned momentary
     LED flashes, and Track Load Deck 1/2 follows the P4 audio-engine loaded
     state with reconnect refresh; post-flash FLX4 behavior smoke for these
     outputs passed on 2026-07-07;
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

## Phase 8: Reviewed Design Plans (implemented)

Two design plans were reviewed against the codebase on 2026-07-03 and have since
been implemented in firmware.

- **S3 XIAO GPIO21 user status LED** — the original GPIO48 WS2812 assumption was
  replaced during the XIAO ESP32S3 migration. The implemented S3 `status_led`
  component now drives the XIAO active-low GPIO21 user LED for reduced FLX4 USB
  link and MIDI input activity feedback and intentionally carries no playback
  state.
- **WAV + FLAC playback** — implemented through format detection,
  `audio_decoder_t`, WAV parsing, and
  `dr_flac` on top of the bounded-cache/ring/resampler/mixer path. MP3 keeps
  PVBR seek support; WAV/FLAC use decoder metadata while Rekordbox/ANLZ remains
  the source for beatgrid/BPM/waveform data. The current WAV subset is classic
  RIFF/WAVE linear PCM16, mono or stereo; 24/32-bit, float and
  `WAVE_FORMAT_EXTENSIBLE` are rejected.

Both plans carry a "Sažetak ispravaka vs original" table documenting every
correction made during the code review.

## Phase 9: Wi-Fi Web UI, Waveform Visualisations, And Audit Hardening (2026-07-04)

Implemented and hardware-verified on COM15/COM3; all P4 and S3 host tests pass.

- **ESP-Hosted Wi-Fi + web UI re-enabled behind a Settings switch.** The onboard
  ESP32-C6 (SDIO ESP-Hosted) SoftAP `Pajoniiir` and the httpd mobile controller
  (`http://192.168.4.1`) were un-parked. A new `wifi_remote` NVS setting (default
  **off**) gates it; the Settings tab switch calls `wifi_link_request_enable()`
  (async worker task, so the ~1-2 s SDIO/C6 bring-up never blocks the LVGL task),
  and `wifi_link_start/stop` bring the whole stack (hosted + Wi-Fi + web + captive
  DNS) up/down. UI stays decoupled via `ui_settings_set_wifi_toggle_cb()`.
- **Web UI batch 1.** Live connection dot, per-deck `duration_ms` + progress bar
  with tap-to-seek, throttled sliders, an honest master VU from the limiter peak,
  and stacked LOAD D1/D2 buttons so both are visible on a phone. The web UI is
  deliberately kept a simple remote, not a full control surface.
- **USB-disconnect crash fix.** `track_meta_cache_load/save` did an ungated
  `stat()` on the USB ANLZ paths; a drive glitch during it tore down the MSC
  device under an in-flight transfer and panicked the vendored driver, wedging
  USB until a power cycle. The stats are now wrapped in `media_io_gate`. A
  Settings "Last reset" readout (`esp_reset_reason()`) was added for on-screen
  crash diagnosis without serial.
- **Audit pass** (thread-safety + robustness). RELAXED atomics for all shared
  mixer/pfl/beat-fx/gain state in `audio_engine` and stats in `p4_audio_link`;
  `deck_core` moves heavy UI commands (LOAD/BROWSE) onto a dedicated lower-priority
  task so the real-time control loop stays responsive; `ae_fail_load()` aborts a
  stalled load; beat-FX beat inc/dec now resyncs the echo delay; `control_link`
  semantic send propagates UART errors; web status JSON is dynamically sized.
- **Overview waveform visualisations.** "Punchy" colour scheme with white
  transient tips; active + armed loop-region amber highlight with edge markers
  (`deck_core_get_loop_display()`); hot-cue markers baked into the large strip and
  as LVGL lines on the mini; translucent warm-amber played-progress overlay on the
  mini. All large-waveform overlays are baked into the scrolling RGB565 strip so
  they PPA-blit atomically (no LVGL-over-PPA flicker).

## Phase 10: P4 Settings And Overview UI Polish (2026-07-08)

Status: implemented, committed to `master`, pushed, and flashed through
`RC1-51-g6fb0bc4f` on P4 `COM15`.

- **Settings cleanup.** Out-of-scope Key Shift UI was removed from the active
  product surface, the retired monitor-speaker switch was removed, wireless
  switches keep dark off states instead of white inactive blocks, and the lower
  mixer/PFL routing block was collapsed into a compact status strip.
- **Overview title and deck data polish.** The blue top strip now uses compact
  bounded content with a remaining-time pill and clearer BPM/pitch readouts.
  Pitch percent was enlarged so it remains readable on the P4 screen.
- **Overview VU placement.** Deck VU meters were moved into a narrow left-side
  channel separate from Play/Cue, then the deck labels were shortened from
  `DECK 1`/`DECK 2` to compact `D1`/`D2` badges to remove the final overlap.
  Static layout guards cover the VU channel, transport targets, and badge
  positions.
- **Overview Beat FX rail.** The former larger right-side Beat FX block was
  replaced by a compact rail that shows the P4-owned effect, target, beat/depth,
  and enabled state without colliding with waveform or transport content.
- **Build/flash acceptance.** Each UI slice passed the P4 ESP-IDF build before
  commit. The latest flashed UI polish is `RC1-51-g6fb0bc4f`, following the Beat
  FX rail (`RC1-49-g72c7985c`) and VU placement (`RC1-50-g1e4cd72b`) flashes.

## Phase 11: Data-Driven Multi-Controller Platform (2026-07-09)

Status: firmware side implemented, host-tested, committed to `master`, pushed,
and flashed; profile-loading path hardware-verified (P4 `/api/status` reports
`profiles:1`). The Hercules Inpulse 500 profile is host-qualified against the
official MIDI specification, but physical controller/audio acceptance remains
open. Windows Profile Builder remains out of firmware scope. Design source:
`Plan 2` (multi-controller with S3 kept as
generic controller host). Format spec: `docs/CONTROLLER_PROFILE_SCHEMA.md`.

Goal: support DJ controllers beyond the DDJ-FLX4 without a firmware rebuild, by
moving the controller-specific MIDI/LED mapping into data profiles the S3
executes. The FLX4 becomes the first profile; its built-in C map stays as a
fallback. The P4 remains the sole authority for deck/audio/UI/mixer state.

- **Profile format + compiler.** `docs/CONTROLLER_PROFILE_SCHEMA.md` defines the
  editable `profile.json` and the compact binary `profile.s3bin` (S3CP: 32-byte
  header + 16-byte input entries + 12-byte output entries + CRC32).
  `tools/controller_profile/compile_profile.py` compiles JSON → S3BIN (+`--dump`).
  A hand-written FLX4 `profile.json` transcribes `flx4_map.c` +
  `flx4_led_midi.c`.
- **Parser + matcher (S3).** `controller_profile` is a pure-C S3CP parser and
  table-driven MIDI-in matcher + LED-out mapper. A golden-parity host test
  proves the compiled FLX4 profile is byte-equivalent to the built-in map
  (12288-message input sweep, snapshot parity, 690-combo LED parity).
- **Profile manager (P4).** `controller_profile_manager` scans
  `/sd/controllers/<name>/profile.s3bin` at boot, validates headers, keeps a
  registry, and matches the connected controller by VID/PID.
- **0xA6 bulk transport.** A variable-length frame layer on the same UART
  (`ctrl_bulk.c`, byte-identical both sides) carries the S3→P4 controller
  descriptor report and the P4→S3 profile transfer
  (BEGIN/CHUNK/END/ACK/NACK/ACTIVATE/STATUS/CLEAR) with CRC16 frames and a
  CRC32-verified reassembly (`cp_xfer.c`). See `docs/CONTROL_LINK_PROTOCOL.md`.
- **Dynamic mapping (S3).** `controller_profile_runtime` holds the active
  profile; `app_main` routes MIDI-in, LED-out, and reconnect input-snapshot
  replay through it when active, falling back to the built-in FLX4 map
  otherwise.
- **Status reporting (P4).** `/api/status` gains a `controller` object
  (present, VID/PID, product, MIDI/audio capability, active profile, profile
  state, and profile count) so the operator can see why a controller works
  without a serial log. `active_profile` is populated only after the S3 ACKs
  `PROFILE_ACTIVATE`; before that, `profile_state` distinguishes `matched`,
  `transferring`, `failed`, and `unsupported`.

Acceptance met: S3 + P4 host tests pass; both `idf.py` builds pass; on hardware
the SD profile loads into the P4 registry and `/api/status` reports `profiles:1`.
End-to-end with a physical FLX4 connected (descriptor → match → transfer →
activate → dynamic mapping) is wired and builds; P4 now reports an active
profile only after the S3 has ACKed activation, so final controller-attached
smoke must confirm `/api/status.controller.profile_state:"active"` and
`active_profile:"pioneer_ddj_flx4"`.

## Phase 12: FLX4 USB Audio Product Stabilization (2026-07-09)

Status: implemented on S3, host-tested, product-built, flashed to S3 `COM6`,
and hardware-smoked with the existing P4 product audio build on `COM15`.

Goal: remove intermittent S3 `p4_audio_link` ring overruns observed after the
FLX4 USB headphone path became audible in the full PCM5102A MAIN + FLX4 cue
topology.

Root cause:

- P4 monitor PCM was producing 48 kHz blocks over the P4-to-S3 link.
- The FLX4 USB Audio packet stream could keep draining at its previously
  selected rate after ring streaming had already started because
  `flx4_usb_audio_poll_ring_autostart()` returned early in `RING` mode.
- That left the S3 ring with a producer/consumer rate mismatch; `ring` pinned
  near 4096 frames and `overruns` climbed even while USB transfers completed.

Fix:

- `flx4_usb_audio` now checks the active `p4_audio_link.sample_rate` while in
  `FLX4_USB_AUDIO_MODE_RING`.
- If the FLX4 playback format supports the P4 link rate, S3 applies the endpoint
  sample rate, updates `s_stream_sample_rate`, and reinitializes the UAC
  packetizer so isochronous packet sizing matches the P4 monitor stream.
- Unsupported or failed rate changes are logged once per rate instead of
  flooding the console; the current stream rate is kept as a fallback.
- Host regression coverage asserts that an already-started ring stream follows
  a supported P4 link rate and ignores an unsupported one.

Acceptance met:

- `tests/run_s3_host_tests.ps1` passes, including `flx4_usb_audio_runtime`.
- S3 product build with `sdkconfig.defaults;sdkconfig.flx4_hp_e2e` passes.
- S3 was flashed on `COM6`.
- Product smoke: over roughly two minutes of playback, S3 logs showed
  `P4_AUDIO_LINK overruns=0`, `gaps=0`, `crc=0`, ring fill oscillating around
  1024-2048 frames instead of pinning at 4096, and `FLX4_USB_AUDIO skipped=0`
  / `underrun=0` while submitted/completed USB packet counts rose together.

## Phase 13: P4 Master Tempo / Key Lock (2026-07-12)

Status: implemented, host-tested, built, flashed to P4 COM15, and basic
single-deck hardware listening smoke passed. A simultaneous dual-deck MT soak
is intentionally deferred.

- Each Overview deck has a compact `MT` toggle beside its pitch readout. The
  callback enters the deck event queue, so UI and the inherited Master Tempo
  button share one P4-owned state transition.
- `audio_keylock` implements a deck-local WSOLA-style granular reader over the
  canonical PCM timeline. A local correlation search aligns overlap regions,
  avoiding the phase cancellation produced by blind overlap-add.
- Tempo advancement remains driven by the selected fader factor while the
  short grains retain the source pitch. Scratch is still the higher-priority
  output source. Near EOF, where the look-ahead window cannot be filled, the
  normal resampler drains the remaining PCM instead of parking in silence.
- Host coverage checks +10% source-frame advancement, retained tone frequency,
  and sample-identical unity-rate output. The full P4 host suite and firmware
  build pass; the flashed image reports 52% app-partition headroom.
- Hardware acceptance confirmed the Overview toggle and expected basic
  key-lock behavior without an observed reboot or audio failure. Longer
  dual-deck quality/CPU tuning remains a future smoke test.

## Phase 14: Dual-Target OTA Foundation (2026-07-13)

Status: partition migration, rollback health-gates, and both HTTP OTA paths are
hardware-smoked. A/B, client-interruption, invalid/oversized-image, forced
rollback, and physical power-loss acceptance pass on P4 and S3.

- P4 retains a wired recovery `factory` image and alternates normal updates
  between two 4 MB OTA slots through the Wi-Fi Remote web application.
- S3 alternates between two 1.875 MB OTA slots. Its temporary Debug AP exposes
  a separate `/update` page so the live SSE log connection cannot monopolize
  the firmware upload workflow.
- Both upload paths use mandatory target headers, bounded streaming receives,
  inactive-slot writes, ESP-IDF image verification, and delayed restart only
  after selecting a validated boot partition.
- S3 additionally rejects a non-ESP32-S3 header before erasing flash and checks
  the `control-board-s3` project name before activation.
- P4 now applies the matching ESP32-P4 header and `main-deck-p4` project checks.
- S3 sends a periodic `0xA6 FIRMWARE_REPORT` carrying its real slot, image
  state, and version. P4 Settings and `GET /api/firmware` expose both targets.
- Wired smoke confirmed P4 recovers `ota_0 / VALID` S3 metadata after a P4-only
  restart; the first decoded report arrived at 3150 ms.
- The release packager validates chip IDs, project metadata, matching versions
  and slot limits, then emits ECDSA P-256 signed `.ddjota` bundles and a signed
  outer release manifest. Both devices verify the embedded manifest before
  flash erase and the streamed image SHA-256 before activation.
- Newly booted OTA images are marked valid only after mandatory subsystem
  startup reaches the shared `firmware_health_mark_ready()` gate.
- AP acceptance passed with release `RC1-105-gf1c176e2`: P4 cycled
  `factory -> ota_0 -> ota_1`, S3 cycled `ota_0 -> ota_1 -> ota_0`, and both
  targets remained on their current valid slot after a client disconnected at
  64 KiB during a write to the inactive slot.
- Test-only `ROLLBACK-TEST-*` images restarted before OTA confirmation and both
  bootloaders returned to the prior valid production slot. The guarded Kconfig
  option is off in normal P4 and S3 builds.
- Removing system power during an inactive-slot write left the current valid
  image bootable on both targets. The accepted S3 run used a 20 KiB/s throttled
  upload so power was demonstrably removed before transfer completion.

Batch 6 signed OTA is hardware-complete as of 2026-07-14: common manifest
parsing/verification, both target integrations, packaging, isolated release
builds, positive updates, rejection cases, interrupted transfers and forced
rollback pass. The `rel-001` private key has an offline USB backup; production
provisioning, encrypted or hardware-backed custody and rotation remain future
hardening. See `docs/OTA_UPDATE_PLAN.md` for the acceptance record.

## Phase 15: Audio Remediation R1 — EOF Drain And Replay (2026-07-13)

Status: implemented, host-tested, P4 firmware-built and basic hardware-smoked.

- Decoder EOF no longer clears the deck's `playing` state. It only means the
  producer has no more source frames; the shared output task remains active
  until the canonical PCM timeline/ring has drained.
- A separate deck-local `playback_finished` flag is set by the output consumer
  only when decoder EOF is present and no decoded future PCM remains. Paused,
  platter-held and scratch-active decks cannot be falsely completed.
- PLAY rewinds to 0 only after consumer-confirmed natural completion. A short
  track that was fully decoded before its first PLAY retains its buffered audio
  and starts normally instead of being mistaken for a replay.
- `playing`, `paused`, decoder `eof`, and `playback_finished` accesses now use
  acquire/release atomic helpers across control, decode and output tasks.
- Natural completion and replay reset the deck resampler and invalidate the
  key-lock reader so stale held/interpolated samples cannot leak into the next
  playback generation.
- A follow-up 45-second dual-deck capture found `IDLE0` watchdog warnings every
  five seconds while `ae_output` was inside key-lock/EQ/mixer DSP. The key-lock
  hot path used software-emulated `double` arithmetic on the P4's single-
  precision FPU. Its WSOLA coordinates now use a small relative `float` window
  plus a 64-bit absolute origin and periodic rebase, preserving long-track
  position accuracy while keeping DSP on the hardware FPU.
- Continuous output also forces one real scheduler delay after at most 64 busy
  blocks. Unlike `taskYIELD`, this gives lower-priority `IDLE0` a guaranteed
  watchdog service window without adding a delay to every audio block.
- Post-flash dual-deck Master Tempo serial acceptance ran for 45 seconds on
  2026-07-13 with no `task_wdt`, no 100+ ms WDT-induced output stalls and no
  monitor-link drops. Throughput held at approximately 172 blocks/s, matching
  44.1 kHz / 256 frames. Only two isolated 11.7/12.2 ms late warnings appeared
  during refill/state activity and did not recur.
- `audio_eof_policy` isolates the completion/rewind decision for host testing.
  The P4 host runner also guards against direct non-atomic writes to the four
  transport flags.

Software acceptance:

- focused `audio_eof_policy` test passes;
- key-lock tone/tempo and 100-million-frame rebase tests pass; the P4 key-lock
  object has no unresolved software double-arithmetic helpers;
- 45-second dual-deck MT hardware/serial watchdog acceptance passes;
- full P4 host suite passes, including 318/318 `audio_engine` assertions;
- isolated ESP-IDF P4 build passes; `main-deck-p4.bin` is `0x205db0` bytes with
  49% free in the smallest app partition.

Hardware acceptance still required:

- [x] Play a track through its actual last sample and confirm the former roughly
  two-second early cut is gone; accepted on hardware 2026-07-13.
- [x] Press PLAY after natural completion and confirm clean restart from 0;
  accepted on hardware 2026-07-13.
- repeat with Master Tempo off/on and with a short track that can be completely
  predecoded before first PLAY;
- confirm pause, platter hold and scratch near EOF do not falsely end the deck.

## Phase 16: Audio Remediation R2 — PCM Timeline Concurrency (2026-07-13)

Status: implemented, host-tested, P4 firmware-built and basic hardware-smoked.

- Random Master Tempo reads no longer combine an atomic logical
  `oldest_seq` snapshot with a separately changing producer-owned physical
  `oldest_index`. The redundant physical eviction cursor has been removed.
- The output task now derives the retained frame's physical slot from its own
  stable `play_seq`/`play_index` anchor. This keeps the key-lock hot path free
  of division while preventing mixed-generation cursor pairs.
- After copying a stereo frame, the reader rechecks `oldest_seq`. If the
  decoder evicted that exact frame during the copy, the read is rejected so a
  potentially overwritten or torn frame cannot reach WSOLA.
- Estimated seek now rejects a missing memory/file source instead of allowing
  `fseek(NULL, ...)` on an invalid engine state.
- Hardware serial diagnosis found a separate scratch-freeze priority inversion:
  the priority-6 output task could starve the priority-5 decoder while the
  decoder held `scratch_capture_writing`, causing scratch begin to time out
  after 20 ms and fall back to platter hold. Output now gives the decoder an
  immediate scheduler tick when freeze is waiting on a writer, and canonical
  timeline publication stops at the next frame after observing freeze.
- A later 30-second capture exposed an ESP-IDF v5.5 USB DWC HAL assert when a
  documented `BNAINTR` channel error arrived without `CHHLTD`. P4 now links a
  project-local wrapper for only the channel interrupt decoder: BNA follows the
  HCD's existing recoverable pipe-error path, while every other missing-halt
  invariant still aborts. Global HAL assertions remain enabled.
- Host coverage exercises random reads after repeated physical wraps and
  evictions. Static guards prevent reintroducing `oldest_index` and the unsafe
  seek fallback, and require the prompt writer-release handshake.

Software acceptance:

- focused `audio_pcm_timeline` host test passes;
- full P4 host suite passes, including 318/318 `audio_engine` assertions;
- isolated ESP-IDF P4 build passes; `main-deck-p4.bin` is `0x205a70` bytes with
  49% free in the smallest app partition.

Hardware acceptance still required:

- play both decks with Master Tempo enabled and exercise tempo changes,
  scratch/release and seeks while the decoder timeline wraps;
- listen for isolated clicks or short WSOLA dropouts during aggressive
  dual-deck operation;
- confirm serial output contains no watchdog, decoder or audio-output errors;
- reproduce sustained USB playback/storage activity and confirm BNA recovery
  does not reboot P4 or unmount the media.

Initial hardware acceptance:

- a post-flash 30-second dual-deck playback/scratch capture completed with no
  scratch writer timeout, platter-hold fallback, USB DWC assert, reboot or
  monitor PCM drop; link throughput remained approximately 172 blocks/s;
- two isolated 11.92/12.13 ms output-late warnings did not cause a drop or
  repeat into a sustained overload.

## Phase 17: Audio Remediation R3 — Lossless Priority Touch (2026-07-13)

Status: implemented; both host suites and firmware builds pass, and dual-target
hardware smoke passed on 2026-07-13.

- S3 and P4 priority queues now remove every undelivered jog-touch event for
  the same platter before inserting the latest level. A release can therefore
  never be placed ahead of an older queued press and leave scratch latched.
- When no stale same-platter edge exists, priority insertion first sacrifices a
  coalescible high-rate jog/fader event.
- If a full queue contains button events only, it retries after the drain and
  rebuild, then evicts one oldest event as the final safety net. The final
  priority insertion applies blocking backpressure until the permanent consumer
  makes room, so a concurrent P4-local producer cannot steal the newly freed
  slot and create another touch-drop path.
- Superseded platter levels increment the coalesced counter; actual high-rate or
  arbitrary fallback victims increment the drop counter.
- Static regression guards require the same policy on the S3 MIDI translator
  queue and the P4 control-link queue.

Software acceptance:

- complete S3 host suite passes;
- complete P4 host suite passes, including 318/318 `audio_engine` assertions;
- ESP-IDF S3 signed OTA build passes; `control-board-s3.bin` is `0xE6170`
  bytes with 52% free in the smallest app partition;
- ESP-IDF P4 build passes; `main-deck-p4.bin` is `0x205AF0` bytes with 49% free
  in the smallest app partition.

Initial hardware acceptance:

- both signed OTA-layout builds were flashed over the wired recovery ports and
  every written image passed esptool hash verification;
- user scratch smoke on both platters passed after the dual-target flash, with
  correct operation and no platter remaining latched after release.

## Phase 18: OTA Remediation R4 — AP And Status Hardening (2026-07-13)

Status: implemented; both host suites and signed-layout firmware builds pass,
and dual-target WPA2 hardware smoke passed on 2026-07-13.

- P4 Wi-Fi Remote and the on-demand S3 Debug AP now both use WPA2-PSK with the
  accepted default password `Pajoniiir`; S3 no longer starts an open network.
- P4 and S3 OTA `finish()` policy distinguishes an invalid duplicate/idle call
  from an incomplete active transfer. Invalid calls preserve the authoritative
  state and error from the operation that already ended, while incomplete
  receiving sessions still abort resources and report `FAILED`.
- Release packaging truncates overlong Git-derived versions at a valid UTF-8
  boundary to the 31-byte `esp_app_desc.version` payload limit before signing.
  P4 and S3 source versions must still match exactly.
- Pure host policies cover idle, stale-resource, incomplete and complete finish
  cases on both targets. Static guards require both accepted WPA2 credentials
  and forbid `WIFI_AUTH_OPEN` in the S3 Debug AP.

Software acceptance:

- complete P4 host suite passes, including 318/318 `audio_engine` assertions;
- complete S3 host suite passes;
- OTA signing Python suite passes 6/6 and the packaging helper covers ASCII,
  31/32-byte and multi-byte UTF-8 boundaries;
- ESP-IDF P4 signed build passes; `main-deck-p4.bin` is `0x205B70` bytes with
  49% free in the smallest app partition;
- ESP-IDF S3 signed build passes; `control-board-s3.bin` is `0xE61E0` bytes with
  52% free in the smallest app partition.

Initial hardware acceptance:

- both committed signed-layout images were wired-flashed and every bootloader,
  partition, OTA-data and application write passed esptool hash verification;
- a phone authenticated to the P4 `Pajoniiir` WPA2 AP with `Pajoniiir` and loaded
  the P4 web UI at `http://192.168.4.1`;
- the same phone authenticated to `Pajoniiir-S3-DEBUG` with `Pajoniiir`, loaded
  the live log, and opened the S3 `/update` OTA page successfully.

## Phase 19: R5 Dead-Code And Legacy-Path Cleanup (2026-07-13)

Status: R5A-R5F complete. Both host suites and signed builds pass; matching
`RC1-121-gb7ac66a5` images were wired-flashed and the final dual-target scratch
soak was accepted on 2026-07-14.

Ordered enclosure-readiness and production-hardening continuation batches are
recorded in `POST_R5_PLAN.md`; E1 signed OTA is complete and E2 enclosure wiring
and service readiness is next.

- The call graph, compatibility-path corrections and ESP-IDF size baseline are
  recorded in `R5_DEAD_CODE_AUDIT.md`.
- An audit-only S3 defaults profile proved the imported legacy CDJ
  panel/TinyUSB-device configuration still built. After explicit user approval,
  R5D intentionally retired it and deleted its components, config branch and
  direct TinyUSB dependency.
- The P4 host runner now rejects new production callers of the single-deck
  transport facade and simple mixer entry point before their removal batches.
- R5B removed the public single-deck audio transport/state/error/loop facade,
  its compatibility deck constant and unused `AE_SDL` state. UI/library and
  host-test callers now use the authoritative deck API.
- R5B software acceptance: complete P4 host suite passes with 319/319
  `audio_engine` assertions; signed P4 build is `0x205AF0` bytes with 49% app
  partition headroom.
- R5C removed the master-only and implicit-headphone-level mixer wrappers. All
  production and host paths now use
  `audio_output_mixer_next_full_with_headphone_level()` with an explicit
  headphone level.
- R5E removed the independent scratch PSRAM allocation, decode-copy and
  decoder-seek release path. Canonical allocation failure now preserves bounded
  PCM-ring playback while a jog touch safely degrades to platter-hold. The full
  P4 host suite passes with 330/330 audio-engine assertions; signed P4 image is
  `0x2056E0` bytes with 49% app-partition headroom.
- R5C software acceptance: complete P4 host suite and signed P4 build pass; the
  image remains `0x205AF0` bytes with 49% app partition headroom.
- R5D leaves USB OTG permanently in FLX4 host role and retains only the optional
  raw-logger/translator split. Active LED IDs formerly hidden in the panel
  header now belong to the shared control-link contract.
- R5D software acceptance: complete S3 and P4 host suites pass; clean signed S3
  build is `0xE60E0` (942,185 B total image, 155,651 B DIRAM, 52% slot free),
  and signed P4 remains `0x205AF0` with 49% slot free.
- R5F removed the unused `anlz_walk_usbanlz()` API/walker after confirming that
  production uses the direct `export.pdb` ANLZ path. Final P4/S3 host suites and
  signed builds pass; matching `RC1-121-gb7ac66a5` images were wired-flashed
  with hash verification. P4 is `0x2056E0` and S3 is `0xE60E0`. A final
  45-second simultaneous capture while both platters were scratch-stressed had
  no reset, panic, stack overflow, watchdog, underrun/overrun, link gap or CRC
  error; the user confirmed correct operation on both decks.

## Phase 19B: Enclosure-Safe Controller Profile Updates (2026-07-14)

Status: software implementation complete, signed and deployed in
`RC1-131-gc391e306`; focused remote-update hardware acceptance remains pending.

- Batch 1 adds strict profile IDs, pre-write S3CP/CRC validation and a bounded
  same-directory staging/backup swap with boot-scan recovery.
- Batch 2 adds `GET /api/controller-profiles` and bounded binary
  `POST /api/controller-profile` with explicit overwrite semantics and clear
  400/408/409/500 failures.
- Batch 3 adds the P4 Wi-Fi Remote profile card, upload progress, client-side
  validation and a separate overwrite confirmation.
- Batch 4 makes registry reads snapshot-based, serializes storage against the
  sender task, preserves the live controller descriptor across rescans,
  invalidates the previous activation cache and queues the matched profile for
  S3 transfer again.
- Host coverage includes ID traversal rejection, corrupt/no-overwrite safety,
  successful replacement, interrupted-swap recovery, corrupt-target backup
  restoration and descriptor-preserving reactivation state. The ESP32-P4
  firmware build passes at `0x208020` bytes with 49% of the smallest app
  partition free.

## Phase 20: Full Code-Review Remediation (2026-07-16)

Status: software remediation merged into `master` at `c391e306`, signed and
deployed as `RC1-131-gc391e306`; functional hardware acceptance is
intentionally deferred to the next smoke session.

- S3 USB host ownership and recovery were hardened: MIDI OUT transfers now stay
  on the USB client task, asynchronous control requests own their completion
  context, audio-stream failures return to a recoverable stopped state, and
  queue-pressure/coalescing behavior preserves priority touch transitions.
- Controller-profile install and activation now validate identity and transfer
  state, preserve the previous working profile across failures, and recover
  interrupted atomic swaps without publishing partially installed data.
- P4 library/catalog and ANLZ/PDB paths now validate offsets, row groups,
  allocation results and object lifetimes before publishing data to the UI or
  playback engine. Web library streaming also aborts cleanly on disconnect.
- P4 and S3 mutating HTTP endpoints require an expected local Host plus an
  explicit control marker. Numeric query parsing is strict and range checked;
  JSON output escapes all control bytes; upload, load and control handlers
  propagate malformed input, queue pressure and response-send failures.
- DSP numeric boundaries now cover non-finite gain, pitch, scratch and resample
  inputs, full-width limiter arithmetic and full-scale flanger interpolation.
  Delay, flanger and scratch hot paths avoid unnecessary modulo operations.
- EQ, filter, pitch, jog and Beat/Pad FX control state crosses task boundaries
  through atomic values or block-boundary commands. The audio task snapshots
  controls once per block; filter coefficients are not recomputed after
  convergence; key-lock correlation reuses reference samples and rejects losing
  candidates early; output mixer ratios and headphone controls are prepared
  once per block.
- UART initialization on both targets unwinds a partially installed driver on
  failure. Shared bulk-frame CRC code no longer emits signedness warnings, and
  firmware-only audio helpers are excluded from PC builds.

Software acceptance:

- complete S3 host suite passes, including controller-profile golden parity;
- complete P4 host suite passes, including the full `audio_engine` assertion
  set, all DSP/parser/UI/web/protocol tests and the R5 dead-code audit;
- OTA signing Python suite passes 6/6 and OTA release-helper tests pass;
- clean ESP-IDF v5.5 S3 build passes; `control-board-s3.bin` is `0xE6500`
  bytes with 52% free in the smallest app partition;
- clean ESP-IDF v5.5 P4 build passes; the deployed `main-deck-p4.bin` is `0x209800`
  bytes with 49% free in the smallest app partition;
- `git diff --check` passes.

Deferred hardware acceptance:

- dual-deck scratch, pitch, Master Tempo and simultaneous Beat/Pad FX soak;
- FLX4 USB MIDI/audio disconnect and recovery under queue pressure;
- P4/S3 local web-control, profile upload and OTA mutation guards from a phone;
- UART startup/recovery and long control-link integrity capture.

## Phase 21: Signed RC1-131 Deployment (2026-07-16)

Status: deployment and boot verification complete; focused functional hardware
acceptance pending.

- P4 accepted the signed bundle over OTA with HTTP 200 and moved from
  `ota_0 / RC1-126-g812ad70f` to `ota_1 / RC1-131-gc391e306`.
- S3 accepted the matching signed bundle over OTA with HTTP 200 and moved from
  `ota_1 / RC1-123-g587cd7a1` to `ota_0 / RC1-131-gc391e306`.
- P4 `/api/status` was healthy after reboot. Its direct firmware endpoint
  correctly returned top-level transfer `state=idle`; P4's nested S3 report
  independently confirmed `ota_0 / valid` and the matching version.
- Both signed bundles and the outer release manifest were cryptographically
  verified with key ID `rel-001` before upload.
- This rollout proves artifact, transport, signature, boot and cross-target
  version agreement. It does not replace the fully functional E1 acceptance of
  `RC1-123-g587cd7a1`; Phase 20 plus FLANGER/DELAY and remote profile-update
  functional smoke remains open.

Exact artifact sizes, SHA-256 values and state transitions are recorded in
[`validation/SIGNED_OTA_RC1_131_DEPLOYMENT.md`](validation/SIGNED_OTA_RC1_131_DEPLOYMENT.md).

## Phase 22: DSI-Synchronised Overview Cadence (2026-07-17)

Status: implementation, clean commit, host tests, paired signed release
packaging and exact-P4-image focused hardware re-smoke complete.

- The panel keeps its 34 MHz DPI clock and uses vertical front porch `371`,
  yielding 49.981 Hz for the 576 x 1181 total timing envelope.
- The DPI `on_refresh_done` ISR only sends a task notification. The existing
  LVGL task coalesces missed refreshes and performs `ui_update()` once for each
  delivered pending refresh before normal LVGL timer handling.
- WIN32 keeps its historical 16 ms UI timer. Firmware LVGL timers, touch and
  animations still wake through their own bounded timeout if refresh events
  stop.
- P4 `idf.py build` passed. The P4 host suite passed 363 audio-engine checks
  with zero failures and all remaining P4 host-test groups; its standalone
  Python OTA-signing subset was skipped because `python` was not found by the
  wrapper and is unrelated to this UI/BSP change.
- The A/B winner first ran as factory-slot development build
  `RC1-132-g2b0cfd59-dirty`. During approximately 132 seconds of dual-deck
  Overview playback, COM15 showed no DSI underrun, panic, watchdog, brownout or
  reset, monitor PCM remained `dropped=0`, and the operator reported fluid
  waveforms with neither watery motion nor a visible flash.
- Source commit `bd5e43cef448aab8701363bf2b23f8ddea74c0f8` was then built cleanly
  for both targets as `RC1-133-gbd5e43ce`. P4 and S3 builds passed; the
  canonical packager signed both bundles and the outer manifest with
  `rel-001`, and independent public-key verification passed.
- The release P4 payload (2,137,344 bytes, SHA-256
  `b3dedb8c8bab9782962867d16c179e5e0cabe9d2f83f5816ce1e873551f71b8e`)
  was byte-for-byte identical to the build image and was wired-flashed over
  COM15. More than 71 seconds of active playback ended at
  `submitted=13392 dropped=0 sent=13391`, with no display/runtime fault and
  operator confirmation of no flash or jitter. S3 was not flashed.
- Because deployment was wired by request, this exact-image pass does not
  repeat the P4 HTTP OTA upload or an OTA-slot transition.

The complete A/B table and focused acceptance evidence are recorded in
[`validation/P4_OVERVIEW_DSI_SYNC_SMOKE_20260717.md`](validation/P4_OVERVIEW_DSI_SYNC_SMOKE_20260717.md).

## TODO: Unify P4 Summary And Full ANLZ Metadata Loading

Status: firmware implementation complete on branch `codex/anlz-metadata-merge`
(2026-07-20). One internal `library_resolve_anlz()` now returns a single owned
full ANLZ object (cache load once with high waveform; on a miss, DAT once + EXT
once + one best-effort cache write). `library_load_anlz()` consumes that one
object for both the track summary and the transactional current-metadata
publish, so a failed load no longer erases the previously valid current
metadata. `library_load_current_anlz()` is removed and all callers
(`media_catalog.c`, WIN32 `ui_library.c`) use the single entry point. ESP-IDF
v5.5 P4 build passes (`main-deck-p4.bin` 0x2097f0, 49% app free) and the
`ui_library` host test passes. A dedicated `library.c` host-test harness now
exists: `tests/library_anlz` compiles the real `library.c` against controllable
cache/ANLZ/PDB stubs plus a counting allocator, covering the warm cache hit (no
parse/write), the cold miss (one DAT + one EXT + one write), cache-rejection
fallback, non-fatal cache-write failure, parser-failure preservation of the
previously published current metadata, and balanced ownership across sequential
replacement (no leak/double-free). Still open: the hardware acceptance rows
below (cold/warm timing benchmark, ~50 alternating dual-deck loads, USB fallback
and disconnect/recovery); cache/USB timing is logged via `esp_timer_get_time()`
but not yet benchmarked on hardware.

### Problem

The current local-track load path calls `library_load_anlz()` and then
`library_load_current_anlz()` for the same track. On a warm metadata-cache path
this opens the same `/sd/trackcache/<track-key>/meta.bin` twice, repeats the
summary/VBR/cue/beat-grid read and performs two DAT/EXT signature-validation
passes against the USB medium. On a cold path the first call parses DAT/EXT and
writes the cache, after which the second call immediately reads that new cache
entry again. The cache remains useful, but this duplicate path wastes metadata
I/O and makes its actual benefit harder to measure.

### Implementation plan

1. Add one internal resolver in `components/library/library.c`, for example
   `library_resolve_anlz()`, which returns one owned, full
   `anlz_metadata_t` object plus a cache/USB source result.
2. Have the resolver build the DAT/EXT paths and track key once, call
   `track_meta_cache_load(..., true, ...)` once, and on a miss parse DAT once
   and EXT once before performing one best-effort cache write.
3. Keep `library_load_anlz(library_track_t *track)` as the public entry point,
   but make it consume the one resolved object for both responsibilities:
   populate the track summary fields (precise BPM, duration, low waveform and
   PVBR table), then publish the same full beat/cue/high-waveform object as the
   current metadata.
4. Publish transactionally: resolve into a local object first; only after full
   success swap it into `s_current_meta` under the library mutex. Free the old
   object after the swap and outside the mutex. A failed new load must not erase
   the previously valid current metadata.
5. Remove the `library_load_current_anlz()` declaration and implementation.
   Retain `library_clone_current_anlz()` and `library_free_current_anlz()` so UI
   consumers keep their existing owned-snapshot contract.
6. Replace every paired call in `media_catalog.c` and the WIN32 simulator paths
   in `ui_library.c` with the single `library_load_anlz()` call. Confirm with
   `rg` that no declaration or call of `library_load_current_anlz()` remains.
7. Preserve the existing cache file format and invalidation rules so deployed
   `/sd/trackcache` contents remain compatible. A missing SD card or rejected
   cache entry must continue to fall back to the USB ANLZ parser, and cache
   write failure must remain non-fatal after a successful parse.

### Measurement and host coverage

- Add one bounded timing record around the unified path using
  `esp_timer_get_time()`, reporting track key, `cache` or `usb` source, elapsed
  microseconds and cache-write result without adding high-rate log spam.
- Cover a full cache hit: one cache load with high waveform requested, no DAT or
  EXT parse and no cache write.
- Cover a cache miss: one cache lookup, one DAT parse, at most one EXT parse and
  one cache write.
- Cover cache rejection/corruption fallback, missing SD, non-fatal cache-write
  failure and best-effort EXT failure.
- Verify that a parser failure preserves the previously published metadata.
- Verify two sequential track loads, deep-clone ownership, replacement cleanup
  and absence of double-free or leaked beat/high-waveform buffers.
- Register the new host test in `tests/run_p4_host_tests.ps1` and retain the
  existing Rekordbox ANLZ/cache parser coverage.

### Hardware acceptance

1. Delete only one selected track's cache entry, load it once and confirm one
   miss, one parse and one write with no immediate second cache read.
2. Reload the same track and confirm one full cache hit with no parser or write.
3. Record cold and warm metadata elapsed time over repeated runs on the same
   USB drive and SD card; report median and worst observed time rather than an
   assumed speedup.
4. Load alternating tracks to Deck 1 and Deck 2 and verify precise BPM,
   duration, low and high waveforms, beat grid, hot cues and PVBR seeking.
5. Repeat without the SD card to prove USB fallback, and repeat the existing USB
   disconnect/recovery case because cache signature checks touch the USB MSC
   medium.
6. Alternate at least 50 dual-deck loads while monitoring internal heap and
   PSRAM for ownership leaks or fragmentation.

### Completion checks

- Run the complete P4 host suite.
- Build `firmware/main-deck-p4` with ESP-IDF v5.5.
- Run `git diff --check` and inspect `git status --short`.
- Record what passed and which hardware acceptance rows, if any, remain open.

Expected structural result: a warm load performs one DAT/EXT validation pair
and one SD cache open instead of two of each; a cold load no longer rereads the
entry immediately after creating it. Overall track-to-audio latency must be
measured separately because USB audio preload can remain the dominant cost.

## TODO: Expand The P4 microSD Service Log

Status: core implemented on branch `codex/p4-service-log` (2026-07-21). A new
`service_log` component replaces the old `sd_diag_log` writer (now deleted) with
a bounded structured journal: a dependency-free host-tested format core (event
X-macro inventory + I/W/E severity, a fixed record with up to four numeric args
and a bounded text field, control-character sanitization, key=value line and
boot-header formatting, rotation-size decision); a fixed 128-record FreeRTOS
queue with non-blocking `xQueueSend(...,0)` drop accounting, an atomic seq and
an NVS-persisted boot id; and one low-priority writer task that batches up to 32
records, appends to `/sd/logs/system.log`, rotates four 1 MiB generations and
fflush/fsyncs at most every ~5 s, all serialised through `sd_io_gate`, non-fatal
when microSD is absent. Producers wired: boot/reset-reason/firmware-info header,
SD mount, USB mount/unmount, library load result and the authoritative
TRACK_LOAD_START/DONE/FAILED (metadata source + resolve time from the merged
ANLZ resolver via `library_last_anlz_load_stats`). A read-only guarded
`GET /api/diagnostic-log` streams the journal. All P4 host tests plus the new
`service_log` test pass and the ESP-IDF v5.5 build is clean.

Additional producers wired: a periodic esp_timer health monitor (off the audio
path) emits rate-limited AUDIO_OUTPUT_LATE / AUDIO_UNDERRUN / AUDIO_SAMPLE_RATE
_CHANGED summaries plus LOW_INTERNAL_HEAP / LOW_PSRAM edges from the counters the
audio engine already maintains; the web layer emits P4_OTA_STARTED/VERIFIED/
FAILED, PROFILE_UPLOAD_DONE/FAILED and WEB_LOAD_REQUEST_FAILED; and the Settings
SYSTEM STATUS panel shows a live "SD Log: OK  <n>KB  drop <n>" line.

Controller and control-link producers are wired too: the firmware-only glue in
`controller_profile_manager` emits CONTROLLER_CONNECTED (VID/PID/caps/product),
PROFILE_MATCHED (or an unsupported warning) and PROFILE_TRANSFER_DONE/FAILED,
while the periodic health monitor logs CONTROL_LINK_ONLINE/OFFLINE edges from
the heartbeat-derived `control_link_connected` state. The pure, host-tested
registry half of `controller_profile_manager` now also owns the disconnect
state reset while preserving the scanned profile inventory.

Recorder producers were added on 2026-07-21 (`RC1-171-gacc2aa5a`), closing a gap
where the one subsystem that writes to microSD contributed nothing to the
microSD journal: RECORDING_STARTED (rate, ring slots, free MiB, session),
RECORDING_STOPPED (seconds, dropped blocks, pushes >= 100 us, worst push — so a
session's real-time behaviour can be judged from the log alone), RECORDING_FAILED
on every `start()` exit and both writer fault paths, and RECORDING_RECOVERED from
boot orphan recovery, which makes a power-loss `.part` salvage provable without
pulling the card. All four were confirmed on hardware.

Software follow-ups closed on 2026-07-26: the valid S3
`CTRL_ID_FLX4_CONNECTION=DISCONNECTED` edge now clears the P4 live controller
selection and emits `CONTROLLER_DISCONNECTED`; the P4 UART parser counts valid
frames, shared-sequence gaps, 0xA5 checksum errors and 0xA6 CRC/format errors;
the periodic health monitor records error/gap deltas; and `/api/status` exposes
both `control_link` and `service_log` objects. Pure host tests cover counter
wrap/gap/error behavior, disconnect registry reset and exact JSON formatting.
Still open: the hardware acceptance rows below, including a physical FLX4
disconnect/reconnect and a controlled bad-frame/gap injection. A temporary or
high-rate detailed trace mode remains explicitly out of scope.

### Goal and real-time boundary

Replace the current boot/cache-only `/sd/logs/system.log` writer with a bounded,
structured service-event journal that is useful for field diagnosis without
blocking audio, USB, UI or control-link tasks. The implementation must not
capture the complete `ESP_LOG` stream and must not record MIDI packets, jog
events, audio blocks, continuous VU/ring values or per-frame UI/render data.

Producers enqueue fixed-size events without allocation or waiting. One
low-priority writer task owns the log file and performs all FAT writes:

```text
P4 component -> non-blocking event -> FreeRTOS queue -> SD writer task
```

- Use a fixed queue of 128 structured events.
- Enqueue with `xQueueSend(..., 0)`; a full queue increments a drop counter and
  never stalls the caller.
- Do not allocate memory in producer hot paths.
- Batch approximately 16-32 events per write, flush about every two seconds and
  sync every 5-10 seconds plus before a controlled reboot.
- An unavailable or failing SD logger remains non-fatal for boot and playback.

### Event API and on-card format

Add a stable event API carrying an event ID, severity, four numeric arguments
and one short bounded text field. The internal fixed-size record also carries a
monotonic sequence number, NVS-backed boot ID and uptime in milliseconds. The
writer sanitizes newline/control characters and formats human-readable
`key=value` records, for example:

```text
seq=381 boot=42 ms=184230 level=I event=TRACK_LOAD_DONE deck=1 key=832 cache=hit metadata_us=8200 result=ok
```

Write a schema/boot header once per startup with the log schema, boot ID,
firmware version, running partition and reset reason. Wall-clock time is
optional; boot ID plus monotonic uptime is the required ordering source.

### Rotation and storage bound

Retain four generations:

```text
/sd/logs/system.log
/sd/logs/system.log.1
/sd/logs/system.log.2
/sd/logs/system.log.3
```

Each file is limited to 1 MiB, for an approximate 4 MiB total bound. Only the
writer task may append, close or rotate these files. Keep controller-profile
and track-cache storage outside the log lock.

### Always-on event inventory

Boot/storage events:

- `BOOT`, `RESET_REASON`, `FIRMWARE_INFO`;
- `SD_MOUNTED`, `SD_ERROR`, `LOG_OPEN_FAILED`, `LOG_QUEUE_DROPPED`;
- threshold-crossing `LOW_INTERNAL_HEAP` and `LOW_PSRAM` warnings.

USB/library events:

- `USB_MOUNTED`, `USB_UNMOUNTED`, `USB_MOUNT_FAILED`;
- `LIBRARY_LOADED`, `LIBRARY_LOAD_FAILED`;
- `TRACK_LOAD_START`, `TRACK_LOAD_DONE`, `TRACK_LOAD_FAILED`.

The final `TRACK_LOAD_DONE` event should combine deck, track key, metadata
cache source, metadata time, audio format/sample rate, preload time and result.
Land this metric with or after the unified summary/full ANLZ metadata TODO so
the logger records one authoritative metadata load rather than the current two
passes.

Audio events:

- `AUDIO_LOAD_DONE`, `AUDIO_LOAD_FAILED`, `AUDIO_DEVICE_ERROR`;
- `AUDIO_UNDERRUN`, `AUDIO_OUTPUT_LATE`, `AUDIO_RING_STARVATION`;
- `AUDIO_SAMPLE_RATE_CHANGED`.

Repeated audio anomalies must be aggregated and rate-limited, for example one
five-second summary containing counts and the worst observed value. Normal VU,
limiter and ring-fill samples are not persistent log events.

S3/controller events:

- `CONTROL_LINK_ONLINE`, `CONTROL_LINK_OFFLINE`, `CONTROL_LINK_CRC_ERROR`,
  `CONTROL_LINK_GAP`;
- `CONTROLLER_CONNECTED`, `CONTROLLER_DISCONNECTED`;
- `PROFILE_MATCHED`, `PROFILE_TRANSFER_DONE`, `PROFILE_TRANSFER_FAILED`.

OTA/web events:

- `P4_OTA_STARTED`, `P4_OTA_VERIFIED`, `P4_OTA_FAILED`;
- `S3_OTA_STARTED`, `S3_OTA_DONE`, `S3_OTA_FAILED`;
- `PROFILE_UPLOAD_DONE`, `PROFILE_UPLOAD_FAILED`,
  `WEB_LOAD_REQUEST_FAILED`.

Never persist firmware/profile payloads, HTTP bodies, signing material or other
secrets. Record mutation type and bounded result metadata only.

### Status and retrieval

Expose logger health in `/api/status`: availability, queue depth, dropped-event
count, current-file bytes and last error. Add a compact Settings status such as
`SD Log: OK - 284 KB - dropped 0`.

After the writer is stable, add a read-only `GET /api/diagnostic-log` endpoint.
The writer must flush before the snapshot is streamed, and read/rotation access
must be serialized without blocking event producers. Do not add a remote log
delete endpoint in the first implementation.

### Host coverage

- non-blocking enqueue, ordering and monotonic sequence numbers;
- exact queue-full drop accounting;
- bounded text copy and newline/control-character sanitization;
- stable formatting for every event type;
- batch write, periodic flush and controlled-reboot sync;
- four-generation size-bounded rotation;
- open/write failure recovery and operation without `/sd`;
- rate limiting and aggregation of repeated audio/UART anomalies;
- concurrent producers without record corruption;
- writer shutdown and ownership cleanup.

Register the new host test group in `tests/run_p4_host_tests.ps1`.

### Hardware acceptance

1. Boot with a working SD card and verify the schema/boot record.
2. Boot without a card and confirm normal USB library and playback operation.
3. Exercise USB mount, track load and disconnect/reconnect records.
4. Run at least 30 minutes of dual-deck playback and confirm the logger adds no
   PCM drops, audio late regressions, UI stalls, watchdogs or resets.
5. Produce one controlled track-load failure and verify its bounded record.
6. Use a test build to fill the event queue and verify the drop counter without
   blocking producers.
7. Use a reduced test-only rotation threshold to validate all four generations.
8. Power-cycle during normal logging and verify that retained files remain
   mountable and readable on the next boot.
9. Alternate track loads while monitoring internal heap and PSRAM for leaks or
   fragmentation.

### Implementation order and completion checks

1. Implement the fixed event schema, non-blocking queue and single writer task.
2. Add batching, sync, rotation, status snapshot and failure accounting.
3. Integrate boot, SD, USB and library events.
4. Add authoritative track-load/cache timing after the metadata-load merge.
5. Add rate-limited audio anomalies, then S3/profile and OTA/web events.
6. Add Settings status and finally the read-only download endpoint.
7. Run the complete P4 host suite, build `firmware/main-deck-p4` with ESP-IDF
   v5.5, run `git diff --check`, inspect `git status --short`, and record which
   hardware acceptance rows remain open.

## TODO: Record The P4 Master Output To microSD

> **Shelved 2026-07-24 — compiled out by default.** The feature is complete and
> was functionally accepted, but every remaining problem is the microSD card's
> write latency rather than the firmware's, and it is not on the critical path.
> It now sits behind `CONFIG_AUDIO_RECORDER_ENABLED` (default `n`): the code,
> its host tests and the Settings/API surface all remain in the tree, but
> nothing is compiled or wired in. See "Why it was shelved" at the end of this
> section before turning it back on. Everything below describes the feature as
> built and stays accurate for when it is re-enabled.

Status: firmware implementation complete on branch `codex/p4-master-recorder`
(2026-07-21), signed and OTA-deployed as `RC1-147-gb9bc8134` on P4 (S3 remains
`RC1-146-g75feb6f1`; the recorder is P4-only). Implemented across seven slices:
pure WAV/segment/recovery helpers (host-tested); a single-producer/single-
consumer PSRAM block ring plus writer-drain core (host-tested); the STOPPED/
STARTING/RECORDING/STOPPING/ERROR state machine with a PSRAM-guarded ring
lifecycle and a low-priority writer task; a bounded `sd_io_gate` SD I/O arbiter
with a host-tested admission policy; the post-limiter `master_out` tap in the
audio output task (`audio_recorder_push_master`, a single atomic load when idle)
plus paced idle silence; a real microSD WAV segment sink under `/sd/recordings`
with 1 GiB / sample-rate segment rollover, 10 s checkpointing and atomic
`.wav.part` -> `.wav` finalize; boot `.part` recovery, an NVS-persistent boot id
and a 64 MiB free-space reserve auto-stop; and a Settings RECORD button wired
through `audio_engine_get_output_sample_rate()`. Free-space start gate is
128 MiB. The P4 build passes and the RECORD control is confirmed on hardware.

Functional hardware acceptance passed on 2026-07-21 (P4 `RC1-166-g4bda7976`):
recording was started from the Settings RECORD control and stopped through the
new API after ~54 s of live MAIN output. Counters reported `dropped_blocks` 0,
`dropped_frames` 0 and a ring high-water of 24 of 508 slots (4.7 %), the writer
sustained exactly 176.4 kB/s (44.1 kHz stereo) with the ring draining to 0-2
slots, and `bytes_written` 9 561 088 matched `frames_written` 2 390 272 x 4 B
exactly. The finalized `/sd/recordings/REC_*.wav` was played back off the card
and confirmed to be the expected MAIN mix.

Hardware-matrix row 6 (push timing) was measured on 2026-07-21 with
`RC1-170`/`RC1-171`, which add `push_count` / `push_max_us` /
`push_over_100us` to `GET /api/recording` and to the `RECORDING_STOPPED`
journal record. Over 120 s of dual-deck recording only 9 of 22 593 pushes
reached 100 us (0.04 %), with zero dropped blocks or frames and a 34 % ring
high-water, so the producer-copy target is met. Row 6's decisive clause is "zero playback/output
regression", and as of 2026-07-22 it is **not met**. Two 25-minute soaks with
two decks playing and recording active produced worst output blocks of 320 ms
and 356 ms. Two earlier readings that suggested otherwise were both taken over
windows shorter than the ~2-minute interval between failures and are withdrawn.

The producer side of the row is fine — the push is a cheap bounded copy. The
blocker is the microSD card: a single block write measured 553 ms, and bursts of
eight consecutive ~360 ms stalls drain the whole ring. A candidate replacement
card probes at 28.95 ms worst case. Re-run the row after the card swap; tuning
the ring or the writer beforehand would only paper over it. Row 9 (power-interrupted `.part` recovery) is still
unexecuted, though `RC1-171` now journals `RECORDING_RECOVERED` so the result
will be provable from the log alone.

Deliberately not done: migrating `track_meta_cache` and the controller-profile
paths onto `sd_io_gate`. Those file sections have several exit points, so an
unbalanced `begin`/`end` would deadlock SD permanently, while FATFS already
serializes volume access — only fairness, not correctness, is at stake. The
Settings free-space read is already throttled to 1000 ms.

The guarded recorder API is implemented after all: `GET /api/recording` returns
the snapshot, `POST /api/recording/start` starts at the live MAIN rate (409 when
no rate exists or a session is already running) and `POST /api/recording/stop`
finalizes. Embedding the same snapshot in the large `/api/status` JSON remains
deferred.

Still open: the p99 push-timing and dropped-frame gates under sustained
dual-deck load, the `.part` recovery smoke, and migrating the other `/sd`
consumers (track_meta_cache, controller-profile, service log, Settings
free-space) plus web-handler `admit()` onto `sd_io_gate`. The notes below are
the original design record.

The existing `audio_engine_decode_to_wav()` helper is PC-test-only, rewinds and
decodes Deck 1 offline, and is not a live master recorder. It may supply small
WAV-header helpers after they are extracted, but it must not be enabled as the
firmware recording path.

### Product definition and capture point

Record the exact stereo 16-bit PCM master bus produced for the PCM5102A MAIN
output. The tap belongs after both decks have passed pitch/scratch/key-lock,
EQ, channel filter, Pad/Beat FX, channel/crossfader/master gains and the final
master limiter, and immediately before the existing I2S write. Capture
`master_out`; do not capture `hp_out`, pre-fader deck frames or the offline
decoder output.

The recorder is optional and subordinate to playback. Any allocation, queue,
SD or writer failure must fail/stop the recording while leaving MAIN audio,
headphone cue, UI and controller operation running. The audio output task must
never call FAT/VFS functions, allocate memory, wait for recorder space or take
a storage mutex.

### Component boundary and state model

Add a P4 `audio_recorder` component with a small public API:

```c
typedef enum {
    AUDIO_RECORDER_STOPPED = 0,
    AUDIO_RECORDER_STARTING,
    AUDIO_RECORDER_RECORDING,
    AUDIO_RECORDER_STOPPING,
    AUDIO_RECORDER_ERROR,
} audio_recorder_state_t;

esp_err_t audio_recorder_init(void);
esp_err_t audio_recorder_start(uint32_t sample_rate);
esp_err_t audio_recorder_stop(void);
bool audio_recorder_push_master(const int16_t *stereo, size_t frames,
                                uint32_t sample_rate);
esp_err_t audio_recorder_get_status(audio_recorder_status_t *out);
```

`audio_recorder_push_master()` is the single-producer real-time boundary. It
copies one already-rendered master block into preallocated storage and returns
immediately. It performs no logging, formatting, file operation, allocation or
blocking synchronization. A full ring increments an atomic drop counter,
requests recorder shutdown and returns false; the caller continues normal I2S
output.

Start is allowed only when `/sd` is mounted, a writable recordings directory
can be prepared, minimum free-space and PSRAM checks pass, and the output
sample rate is known. Recording is never restored automatically after reboot.

### PCM ring and writer task

- Use a fixed single-producer/single-consumer ring in PSRAM. Start with a
  512 KiB target, providing about 2.7 seconds at 48 kHz stereo PCM; make
  256 KiB, 512 KiB and 1 MiB build-time choices for hardware comparison.
- Allocate the complete ring before entering `RECORDING`; never resize it while
  active. Allocation failure returns a visible `NO MEMORY` start error without
  changing playback state.
- Store fixed 256-frame blocks matching `AE_OUT_FRAMES`, with sequence and
  sample-rate metadata published through release/acquire indices so rate
  transitions remain ordered with PCM.
- Run one low-priority writer task below audio/decode and LVGL priorities. Begin
  on the non-audio core and change affinity only from measured evidence.
- Drain PCM in 32-64 KiB sequential batches. The writer owns the open recording
  file, WAV header updates, sync, segment changes and final rename.
- Report ring used/high-water, dropped blocks/frames, bytes written, elapsed
  recording time, current segment, sample rate and last error through a copied
  status snapshot.

The output task currently skips rendering when both decks are inactive. While
recording, preserve the recording timeline by producing/pushing correctly
paced silent master blocks at the established output rate. Do not busy-loop;
retain I2S or calculated block-period pacing. Starting REC before an output
rate exists should fail rather than invent a rate.

### WAV files, segmentation and crash recovery

Store recordings under:

```text
/sd/recordings/REC_B<boot-id>_<session>_<segment>_<rate>Hz.wav.part
```

Use PCM WAV, stereo, signed 16-bit little-endian at the actual master output
rate. On clean segment completion, patch the RIFF/data sizes, flush/sync and
atomically rename `.wav.part` to `.wav`.

- Limit a segment to 1 GiB, safely below the classic RIFF/FAT 4 GiB boundary;
  continue automatically in the next numbered segment.
- A master output sample-rate change closes the current segment and opens a new
  segment at the new rate. Never place multiple PCM rates inside one WAV file.
- Checkpoint the WAV sizes and sync approximately every ten seconds so a sudden
  power loss leaves a bounded repair window without excessive FAT traffic.
- At boot, scan only `/sd/recordings/*.wav.part`, validate the placeholder
  header and file length, truncate an incomplete stereo frame if necessary,
  patch sizes and rename the result with a recovered marker. Never rewrite an
  already final `.wav` file.
- Use an NVS-backed boot ID plus session/segment counters because wall-clock
  time may not be available. A valid clock may additionally supply a display
  label but is not the uniqueness source.

At 44.1 kHz the raw data rate is 176.4 kB/s (about 635 MB/h); at 48 kHz it is
192 kB/s (about 691 MB/h). Query free space in the writer/storage context, not
the LVGL task. Start with at least 128 MiB free and stop cleanly at a 64 MiB
reserve, with both thresholds centralized for later product tuning.

### Shared microSD coordination

Introduce a small SD I/O gate/arbiter before enabling continuous recording.
Recorder, `track_meta_cache`, controller-profile install, service-log writes
and free-space queries must use the same storage boundary for administrative
and FAT operations. The recorder acquires it only around bounded 32-64 KiB
writes and releases it between batches so cache/profile/service work can run.
No audio producer ever acquires this gate.

- Raise the SD VFS `max_files` from the current four only after auditing the
  simultaneous recorder, service log, cache/profile and web-download handles.
- Move Settings free-space polling away from direct `f_getfree()` on the LVGL
  path; publish a cached storage/recorder status instead.
- Reject profile upload or log download with a clear busy/error result if the
  bounded storage policy cannot service it safely during REC; never allow such
  operations to back-pressure audio.
- Detect write/card-removal failures, close or abandon the `.part` file as far
  as the medium permits, set recorder `ERROR` and disable further pushes.

The planned structured SD service logger should reuse this arbiter later. The
recorder must not depend on the service-log TODO being complete first.

### Controls and operator feedback

Add explicit start/stop controls to the P4 UI and guarded web API; do not infer
recording from deck play state and do not add an FLX4 mapping without a separate
documented control decision.

- Show a persistent red `REC` indicator outside transient status text while
  active, plus elapsed time and remaining/free-space summary.
- Refresh time/space at no more than 1 Hz and invalidate only changed labels;
  do not add frame-rate animation that could disturb Overview waveforms.
- Show `STARTING`, `STOPPING`, `SD FULL`, `SD ERROR`, `NO MEMORY` and dropped-PCM
  failure states explicitly. REC must never remain visually active after the
  component has stopped accepting PCM.
- Add guarded `POST /api/recording/start` and `/api/recording/stop`; expose the
  copied recorder snapshot under `/api/status`. Repeated start/stop requests
  must be idempotent or return a precise conflict.
- Stop transitions disable new pushes first, drain the finite ring in the
  writer, finalize the header, sync, rename and only then publish `STOPPED`.

### Host and integration coverage

- WAV header encode/patch, exact stereo byte counts and little-endian samples;
- SPSC ring wrap, ordered block metadata and concurrent producer/consumer;
- non-blocking full-ring behavior, exact drop accounting and auto-stop request;
- start rejection for missing SD, unknown rate, insufficient space and failed
  PSRAM allocation;
- normal start/stop, stop-with-backlog drain and repeated control requests;
- 44.1/48 kHz rate transition producing two independently valid WAV files;
- 1 GiB boundary logic using a reduced test threshold;
- silent-block timeline behavior while no deck is active;
- write/open/sync/rename/card-removal failures without playback-state changes;
- `.wav.part` recovery, malformed-part rejection and idempotent reboot scan;
- status snapshot consistency and 1 Hz UI update planning;
- SD gate fairness between recorder batches and cache/profile/log clients.

Register new tests in `tests/run_p4_host_tests.ps1`. Keep the PC offline
`audio_engine_decode_to_wav()` tests separate so they cannot falsely satisfy
live post-mix recorder coverage.

### Hardware acceptance and performance gates

1. Record and play back single-deck 44.1 kHz and 48 kHz material; validate WAV
   headers, duration, stereo channels and absence of discontinuities.
2. Record the dual-deck master while exercising channel faders, crossfader,
   EQ/filter, Pad/Beat FX, loops, scratch, pitch and Master Tempo; verify the
   file matches the audible RCA master behavior and excludes headphone PFL.
3. Load mixed-rate decks and force an output-rate transition; require clean
   segment finalization and no invalid mixed-rate WAV.
4. Verify recorded silence and continuous duration across a paused/no-active-
   deck interval.
5. Run at least 60 minutes of worst-case dual-deck recording using the normal
   20 MHz four-bit SDMMC configuration. Require recorder dropped frames `0`,
   monitor PCM dropped `0`, no new audio-output late events, DSI underruns `0`,
   no watchdog/reset and operator confirmation of fluid, flash-free waveforms.
6. Capture recorder push timing. Target p99 below 100 us per 256-frame block;
   the decisive gate remains zero playback/output regression.
7. Record ring high-water and writer latency with the selected production SD
   card and one deliberately slower card. A sustained/full ring must stop REC,
   not audio.
8. Exercise low-space stop, SD removal/write failure, queue saturation and
   controlled stop while both decks continue playing.
9. Interrupt power during recording, reboot and verify bounded `.part` recovery
   without modifying completed recordings.
10. Compare internal heap, largest free block and PSRAM before/during/after REC,
    including two of the largest supported track preloads; require no leak and
    document the chosen 512 KiB/1 MiB ring tradeoff.

### Implementation order and completion checks

1. Add pure WAV/segment/recovery helpers and host tests.
2. Add the SPSC PCM ring, recorder state machine and writer with fake storage.
3. Add the bounded SD arbiter and migrate direct free-space/cache/profile
   administrative operations needed for safe concurrency.
4. Wire the post-limiter `master_out` tap and inactive-deck silence behavior.
5. Add real SD file handling, checkpoint/finalize/recovery and status counters.
6. Add UI/API controls and low-rate feedback.
7. Run the complete P4 host suite and an ESP-IDF v5.5 P4 build.
8. Complete the hardware matrix above before marking recording production-
   ready; update `ARCHITECTURE.md`, `STARTUP_CHECKLIST.md`, `RISK_REGISTER.md`,
   `DOCUMENTATION_STATUS.md` and the P4 component guide with measured results.
9. Run `git diff --check`, inspect `git status --short`, and explicitly list any
   hardware rows not executed.

### Why it was shelved (2026-07-24)

The recorder needs 176 kB/s. Cards deliver 12 MB/s. Throughput was never the
issue — the issue is that a card stops answering for hundreds of milliseconds at
a time while it does internal housekeeping, and a burst of those drains the
2.95 s ring no matter how the writer is arranged.

Every firmware-side contribution was found and eliminated, and none of them was
the cause:

| tried | result |
| --- | --- |
| PSRAM write staging | made it **worse**: 553 ms -> 1735 ms (bus contention) |
| checkpoint no longer patches the WAV header | no change: 369.8 ms -> 375.8 ms |
| stall-burst coalescing + drop rate limiting | fixed a **self-inflicted** loop: stall records were being written to the same card under the same gate, so a burst of stalls generated extra card transactions exactly when the card was already behind (gate_wait 8 ms -> 185 ms) |
| reduced journal writes | helped the system, not the recorder |

After all of it, roughly **370 ms of `fwrite`** survived, and `gate_wait` fell to
single-digit milliseconds — that is, the firmware was no longer in the way at
all.

A replacement card was fitted on 2026-07-24. Early numbers were better
(`fwrite` 73-89 ms over the first five minutes, ring high-water 27/508, zero
dropped blocks) but the run was cut short, so **this is not a completed
measurement** — do not cite it as one. The old card was separately measured on a
PC with `tools/sd_card_latency_probe.ps1`: median 1.93 ms, p99.9 39 ms, but one
stall of **1415 ms**, confirming the failure mode is real even if the PC's
better power delivery makes it rarer than on the P4.

Two things also surfaced during the soak and remain unexplained; anyone
re-enabling this should expect to meet them:

- **Loading a track killed an in-progress recording.** Log shows the load at
  01:39:45 and the recorder at `STOPPED` nine seconds later. Never diagnosed.
- ~~96 kHz/24-bit FLAC fails to load on deck 1~~ — **withdrawn: not a defect.**
  The error is `AUDIO_LOAD_FAILED a1=261 NOT FOUND`; the file is a dead PDB row
  with no file behind it. FLAC decoding works. See `bench-notes.md` for why
  every non-mp3 entry in this library is a dead row and how not to repeat the
  mistake.

To re-enable: set `CONFIG_AUDIO_RECORDER_ENABLED=y`. Note that changing
`sdkconfig.defaults` does **not** modify an existing `sdkconfig` — verify the
flag in the actual build's `sdkconfig`, or a build that looks enabled will
silently ship disabled.

## Phase 23: P4 Pull OTA Through Temporary Wi-Fi STA Mode

Status: core path implemented and proven end to end on hardware 2026-07-24;
production software hardening implemented and host-tested 2026-07-26.
`RC1-254-g21f21963` completed AP→STA→HTTPS channel read→signed bundle
download→verification→inactive-slot flash→reboot, with AP restoration on the
non-install paths. Pull discovery now accepts only a newer monotonic
`RC<tag>-<distance>-g<hash>` offer, expires it after ten minutes, rejects
unorderable/older releases and verifies the downloaded bundle against the
channel size and SHA-256 before activation. Signed local push OTA intentionally
remains the service rollback path. The canonical `pajoniiir.local` mDNS
identity, dynamic AP-IP/mDNS Host allow-list and strict relative bundle-path
validation are implemented with host coverage. Hardware re-smoke and the
remaining extended recovery matrix below are still open.
The current standalone P4 Wi-Fi Remote remains the normal operating mode. The
canonical mDNS hostname is exactly `pajoniiir.local`; do not introduce another
product hostname or a differently spelled alias.

The existing signed `.ddjota` upload over the `Pajoniiir` AP remains the offline
and service fallback. S3 OTA and `Pajoniiir-S3-DEBUG` remain outside this first
implementation batch.

### Operating model and ownership

The P4/C6 Wi-Fi path has two mutually exclusive operational modes:

- `REMOTE_AP`: the existing `Pajoniiir` SoftAP, captive UI and full secondary
  web control;
- `OTA_STA`: a temporary connection to a configured service Wi-Fi network or
  phone hotspot for update discovery and bundle download.

The normal success path is:

```text
REMOTE_AP -> STA_CONNECTING -> UPDATE_CHECK -> DOWNLOAD -> VERIFY -> REBOOT
```

Every non-reboot exit must restore the standalone service surface:

```text
STA_CONNECTING / UPDATE_CHECK / DOWNLOAD / VERIFY failure
    -> RESTORE_AP -> REMOTE_AP
```

Use one worker task as the sole owner of AP/STA transitions. UI callbacks, web
handlers and OTA code submit commands and read copied status snapshots; they
must not call blocking ESP-Hosted, Wi-Fi or DNS lifecycle functions directly.
Serialize rapid toggles, duplicate update requests and concurrent AP/STA
requests.

Keep network mode separate from OTA transfer state. Add explicit models such
as:

```c
typedef enum {
    WIFI_LINK_OFF = 0,
    WIFI_LINK_REMOTE_AP,
    WIFI_LINK_SWITCHING_TO_STA,
    WIFI_LINK_OTA_STA,
    WIFI_LINK_RESTORING_AP,
    WIFI_LINK_ERROR,
} wifi_link_mode_t;

typedef enum {
    P4_OTA_PULL_IDLE = 0,
    P4_OTA_PULL_CHECKING,
    P4_OTA_PULL_AVAILABLE,
    P4_OTA_PULL_DOWNLOADING,
    P4_OTA_PULL_VERIFYING,
    P4_OTA_PULL_READY_TO_REBOOT,
    P4_OTA_PULL_FAILED,
} p4_ota_pull_state_t;
```

### Wi-Fi lifecycle refactor

Split the current all-or-nothing `wifi_link_stop()` path into independently
owned operations for:

- stopping captive DNS and the HTTP service;
- stopping/destroying the AP interface;
- stopping/destroying the STA interface;
- fully deinitializing `esp_wifi` and ESP-Hosted when no mode needs the C6.

An AP-to-STA transition should reuse the active ESP-Hosted transport rather
than tear down and immediately recreate the C6 link. Stop the AP service,
switch the Wi-Fi interface/mode, create the STA netif, register its event/IP
handlers and wait for an authoritative IP event before starting update
discovery.

Fix the existing failure-loop behavior while doing this work. A failed AP or
STA start must not immediately retry forever while `desired != active`. Use a
bounded policy, initially three attempts with 1/2/4-second backoff, then publish
an error and restore the previous stable mode. A later attempt requires a new
operator request.

Before leaving `REMOTE_AP`, reject the request if:

- either deck is playing or the audio engine cannot stop cleanly;
- another OTA transfer is active;
- a controller-profile install/activation is in progress;
- a critical media/storage operation owns the required gate;
- the planned master recorder is active when that component is later added.

Do not perform automatic background update checks during playback.

### Service-network configuration

Persist bounded OTA-client configuration in P4 NVS:

- service-network SSID;
- service-network password;
- update base URL or selected release channel;
- optional last accepted release identifier/security counter.

Never log the password, place credentials in a URL/query string or return them
from a status API. Provide explicit save, replace and clear operations. Validate
SSID, password and URL lengths before persistence. Keep production NVS/flash
encryption as an explicit release-security gate if credentials are retained on
shipped devices.

Allow configuration through the current guarded P4 web UI. The physical P4
Settings view must at minimum provide `CHECK FOR UPDATE`, `CANCEL`, `CLEAR
WI-FI` and a copied low-rate status/progress display; it must not require the
web connection to remain alive after the AP is stopped.

### Mandatory mDNS identity

The P4 target now uses the ESP-IDF mDNS component with:

- hostname `pajoniiir`;
- canonical URL `http://pajoniiir.local`;
- `_http._tcp` service on port 80;
- stable instance name `Pajoniiir`.

The web-server lifecycle registers `pajoniiir.local` while the normal AP web
surface is active and removes it when that surface stops for the STA visit.
The normal AP web UI
must remain reachable through both `http://192.168.4.1` and
`http://pajoniiir.local`.

When P4 is connected to the service network, the web/status surface may also
be reached at `http://pajoniiir.local` by clients on that network. Treat this as
best-effort convenience: an upstream AP may use client isolation, and a phone
hosting a hotspot may not route mDNS or client traffic back to its own UI.
Standalone secondary control remains guaranteed by restoring `Pajoniiir`.

The former fixed `Host: 192.168.4.1` API guard is now a tested dynamic
allow-list containing only:

- `pajoniiir.local`, with an optional numeric port;
- the authoritative active AP IPv4 address, with an optional numeric port
  (normally `192.168.4.1`).

Do not accept an arbitrary hostname merely because P4 is in STA mode. Preserve
the existing mutation header and strict request parsing; mDNS is discovery, not
authentication.

### AP-to-STA operator flow

For an update initiated from the web UI:

1. Validate prerequisites and persist the accepted operation before switching
   modes.
2. Return `202 Accepted` with a clear warning that `Pajoniiir` will temporarily
   disappear and that progress continues on the physical display.
3. Delay the network transition only long enough for the HTTP response to be
   transmitted; do not block the HTTP handler on association or download.
4. Stop the AP web/DNS services and switch the C6 to STA mode.
5. Connect with bounded association and DHCP timeouts.
6. Start mDNS and, where the network permits it, the guarded status/web service
   on `pajoniiir.local`.
7. Check the signed update metadata and require explicit install confirmation.
8. On no-update, cancel or failure, tear down STA and restore the AP, captive
   DNS, web service and mDNS identity.

Show `CONNECTING`, `CHECKING`, `UPDATE AVAILABLE`, download percentage,
`VERIFYING`, `RESTORING Pajoniiir`, `ERROR` and `REBOOTING` explicitly. Do not
leave the Settings switch claiming that the Remote AP is on while it is
temporarily unavailable.

### Pull-OTA component and signed update metadata

Add a P4 `p4_ota_pull` component using `esp_http_client`. It owns server
connection, certificate validation, bounded metadata parsing, download
timeouts, cancellation and copied progress/status. Start with HTTPS server
certificate validation in addition to the existing firmware signature; TLS
protects update-source privacy and availability, while the embedded ECDSA key
remains the firmware-authenticity boundary.

The device-consumable signed update metadata must identify:

- release/channel ID and version;
- target `p4` and project `main-deck-p4`;
- `.ddjota` URL, exact size and SHA-256;
- minimum compatible release where needed;
- a future monotonic security/release counter.

Reuse or extend the canonical release packager so device metadata is generated
from the same artifacts as the existing outer release manifest. Do not create
a separately maintained unsigned update index. Because Git-derived version
strings are not a monotonic anti-rollback value, never auto-install a release
only because its version text looks newer. A downgrade requires an explicit
warning and physical confirmation; a future security counter may block silent
downgrades while preserving a documented service-recovery override.

### One transport-independent `.ddjota` receiver

Do not duplicate bundle parsing and verification between the current HTTP POST
upload and the new HTTP client download. Extract a streaming receiver with an
API equivalent to:

```c
esp_err_t p4_ota_bundle_begin(size_t content_length);
esp_err_t p4_ota_bundle_feed(const void *data, size_t size);
esp_err_t p4_ota_bundle_finish(void);
void p4_ota_bundle_abort(const char *reason);
```

The receiver accumulates only the fixed bundle/image headers, then:

1. validates bundle format, target, ESP32-P4 chip, project, key ID, declared
   size and exact transport length;
2. verifies the ECDSA P-256 manifest signature before `esp_ota_begin()` or any
   flash erase;
3. validates the embedded ESP32-P4 image header;
4. streams the image directly into the inactive OTA slot without buffering the
   complete bundle in RAM or on microSD;
5. calculates and checks the signed image SHA-256;
6. checks the image project name and exact signed version;
7. calls `esp_ota_set_boot_partition()` only after every check succeeds.

The existing AP upload handler and `p4_ota_pull` must both feed this component.
For the first implementation require an authoritative `Content-Length`; reject
chunked, trailing or truncated bundles rather than adding resume complexity.

### Failure and recovery contract

Before boot-partition activation, every network, timeout, cancellation,
allocation, TLS, metadata, signature, length, hash or flash error must:

- abort any open OTA handle;
- leave the running/current boot slot unchanged;
- stop and destroy the temporary STA interface;
- restore `Pajoniiir`, captive DNS, the web server and `pajoniiir.local`;
- publish one bounded operator-visible error and structured service event.

After successful activation, reboot normally and retain the existing
`PENDING_VERIFY` startup-health/rollback behavior. A successful HTTP download
or `READY_TO_REBOOT` state is not functional acceptance; the new image becomes
authoritative only after boot health marks it `valid`.

### Host and integration coverage

- Wi-Fi mode and OTA-pull state transitions, including every recovery edge;
- rapid toggles, duplicate requests and command serialization;
- bounded retries/backoff with no tight failure loop;
- SSID/password/URL validation and credential-redacted status/log output;
- exact `pajoniiir.local` registration lifecycle on AP and STA interfaces;
- dynamic Host allow-list for AP IP, current STA IP and canonical mDNS name;
- fragmented `.ddjota` headers/payload across arbitrary feed boundaries;
- wrong target, chip, project, key, signature, version, size, SHA and trailing
  data rejection before boot-slot activation;
- timeout/disconnect/cancel before and after `esp_ota_begin()`;
- AP restoration after association, DHCP, DNS, TLS, HTTP and OTA failures;
- retained behavior of the existing signed AP upload endpoint;
- copied UI/API status under concurrent reads.

Register new tests in `tests/run_p4_host_tests.ps1` and retain the common OTA
signing/release-helper suites.

### Hardware acceptance

1. Confirm existing `Pajoniiir` web control is unchanged before any OTA-client
   operation.
2. Resolve and use `http://pajoniiir.local` while connected directly to the P4
   AP; retain `192.168.4.1` as the recovery address.
3. Configure a service AP, switch to STA, obtain DHCP and resolve
   `pajoniiir.local` from at least the supported phone and PC workflows where
   the AP permits client communication.
4. Check for an update without installing it, cancel and confirm automatic AP
   restoration.
5. Download a valid signed bundle, boot the inactive slot, confirm exact
   version/slot and wait for image state `valid`.
6. Exercise invalid signature, wrong target, truncated body, trailing data,
   server stall and connection loss. Require no boot-slot change and automatic
   restoration of `Pajoniiir` after every case.
7. Test wrong Wi-Fi credentials, missing DHCP, DNS failure, TLS failure and low
   RSSI without a watchdog, retry storm or permanent loss of the service AP.
8. Interrupt power during download and during first boot, verifying intact
   current firmware and the existing forced-rollback behavior respectively.
9. Confirm update entry is rejected while either deck plays and that no normal
   background check adds audio, UART, UI or DSI timing regressions.
10. Re-run the existing signed `.ddjota` upload through `Pajoniiir` to prove the
    offline fallback remains functional.
11. During repeated AP/STA/AP cycles monitor internal heap, largest free block,
    C6/ESP-Hosted resources and netif/event-handler counts for leaks.
12. Require no PCM drops, audio-output late regression, UART frame loss, DSI
    underrun, watchdog, panic or reset during accepted transitions.

### Implementation order and completion checks

1. Add pure state models, command serialization, bounded retry/backoff and host
   tests; fix the existing AP start-failure loop first.
2. Refactor ESP-Hosted/Wi-Fi/AP service lifecycle without changing the normal
   `Pajoniiir` behavior.
3. [software complete] Add `pajoniiir.local`, dynamic Host validation and
   AP-mode discovery tests; physical client resolution remains an acceptance row.
4. Add bounded NVS-backed service-network configuration and redacted UI/API
   status.
5. Implement AP-to-STA-to-AP transitions and failure restoration before adding
   any remote download.
6. Extract the shared streaming `.ddjota` receiver and migrate the existing AP
   upload handler to it without changing accepted artifacts.
7. Extend the release tooling with signed device-consumable update metadata and
   add `p4_ota_pull` HTTPS check/download support.
8. Add physical UI and guarded web controls, asynchronous `202 Accepted`
   behavior, progress, cancel and explicit AP-restoration feedback.
9. Run the complete P4 host suite, common OTA signing/package suites and an
   ESP-IDF v5.5 P4 build.
10. Complete the hardware matrix before marking pull OTA ready; update
    `OTA-UPDATE.md`, `ARCHITECTURE.md`, `STARTUP_CHECKLIST.md`,
    `RISK_REGISTER.md`, `DOCUMENTATION_STATUS.md` and the P4 component guide
    with measured results.
11. Run `git diff --check`, inspect `git status --short`, and explicitly list
    every hardware row not executed.

## TODO: Revisit Beat FX Delay, Flanger And Echo

Status: **closed 2026-07-24.** FLANGER corrected and hardware-accepted;
DELAY and ECHO confirmed good by the operator without changes. All three
additionally had a measured headroom defect fixed in `RC1-223-gdfa619a9`.

### Flanger outcome (2026-07-24)

Operator listening pass narrowed "all three sound weak" to FLANGER alone:
"provjerio sam i samo FLANGER nije dobro". The plan's own advice held - the
problem was mapping, not algorithm - but with one twist the plan did not
anticipate.

What was actually wrong, in the order it was found:

1. **Wet mix far too shallow.** The comb notch is `20*log10(1-wet)`, so the
   shipped 0.50 gave only -6 dB: a phasey wobble, not a flanger. Raised to
   0.70 (-10.5 dB). 0.90 was tried and rejected by ear as "zagušen".
2. **Sweep floor too low in frequency.** The first notch sits at
   `1/(2*delay)`, so the 600 us minimum delay capped the sweep at 833 Hz and
   kept the whole effect in the low mids. Lowered to 250 us.
3. **Output normalisation was actively harmful.** Dividing by `1/(1+wet)` meant
   turning the depth knob up made everything quieter and duller. Removed; dry
   now stays at unity and wet is added on top, as on hardware.
4. **The real missing piece was not in the DSP at all.** After the above, the
   operator still heard it only as "jet na pola". It came good on a *faster
   BEAT setting*: "s bržim beatom se čuje jet". A slow sweep spreads the
   resonance over so long a period that it reads as tonal drift rather than
   movement. Worth remembering before re-tuning Delay and Echo: the beat
   selector is part of how these effects are judged, and testing at one beat
   size can condemn a correctly-tuned effect.

An automated swing metric was built during this pass and **ranked the
worst-sounding configuration highest**. It was discarded. The lesson for the
remaining two effects: a metric can confirm a comb notch exists, but it cannot
decide whether the result sounds like a flanger.

Two things were tried and measured as *not working*, recorded so they are not
retried: a wet-signal normalisation (above), and a one-pole low-pass in the
feedback path intended to bound the resonance - it attenuates highs while the
resonant peak sits at 400-1000 Hz, so the measured peak did not move at all.

**Clipping defect found by measurement, not by ear.** The accepted tuning has a
resonant gain of **3.34x** (theoretical ceiling `1 + wet/(1-fb)` = 3.8x). The
test track peaks near 16% of full scale so the operator never heard it, but on
loud material the sum hits the int16 ceiling and hard-clips *inside*
`audio_flanger_fx_process_frame`, ahead of the master limiter, where nothing
downstream can catch it. `/api/status` corroborated it: `limiter_samples=5`,
`limiter_peak=32189` after the tuning session. Fixed with a quadratic soft knee
at 0.75 FS on both the output and the feedback write - identity below the knee,
so the accepted tuning is bit-exact at normal levels, and rolling to zero gain
at full scale above it. Host tests now bound the resonant peak on both sides
(2.8x-3.6x) so it cannot be quietly tuned away again.

### DELAY and ECHO outcome (2026-07-24)

Both confirmed good by the operator on hardware - "oni su dobri, provjereno" -
with no DSP or mapping change needed. The original report of all three sounding
weak resolved to FLANGER alone.

They were **not** left untouched, though. Both share the flanger's structure -
dry at unity with wet added on top - so a sustained signal builds to
`1 + wet/(1-feedback)`. Measured at full depth: **3.18x for ECHO**, 1.70x for
DELAY. On a signal at half full scale, ECHO pinned **47% of its output samples**
against the int16 ceiling, hard-clipping inside the effect where the master
limiter cannot reach it. The same soft knee was applied.

This is the second time in one session that a listening pass passed an effect
that measurement then failed, for the same reason both times: the reference
track peaks near 16% of full scale, so nothing ever approached the ceiling.
**An ear acceptance does not cover headroom.** Any future effect that adds wet
on top of unity dry should have its peak gain measured before it is called
done.

### Original plan (kept for reference)

Operator reports all three sound weak in use. None
of them has ever had a physical audio acceptance — `DOCUMENTATION_STATUS.md`
has carried "Flanger and Delay are software-tested and OTA-deployed, with
focused physical audio/target/beat/depth smoke pending" since they landed, and
Echo's acceptance predates the 2026-07-10 DSP pass that changed its feedback
damping and taper. So "sounds weak" is plausibly the first real listening test
any of them has had.

### Do not start by changing the DSP

The single most likely explanation is not the filter maths. Depth, wet gain and
beat time all pass through mappings that were tuned by reading code rather than
by ear, and a wet signal that tops out too low sounds identical to a broken
effect. Establish where the signal actually is before touching any algorithm.

### Diagnosis first

1. Confirm each effect is reaching the audio path at all. Feed a known input,
   enable one effect at full depth, and capture the master output with the
   microSD recorder. A `.wav` is objective in a way that a listening impression
   is not, and the recorder is already there.
2. For each of FILTER, ECHO, FLANGER, DELAY, record what actually changes
   between depth 0 and depth 127: peak level, and whether the effect is audible
   at all in the capture. Do this per target (CH1, CH2, `1&2`) — the combined
   target derives its timing from Deck 1 only, which is a known asymmetry.
3. Check the parameter journey end to end for one effect before generalising:
   FLX4 MIDI value -> `flx4_map` -> control-link frame -> `deck_core` ->
   `audio_engine_set_beat_fx_*` -> the DSP's own scaling. A value flattened at
   any stage produces exactly the reported symptom.
4. Note the existing suspicion recorded in the P4 guide: Echo maps feedback
   across 0.20-0.68 and wet tops out at 0.70, and depth uses a sqrt taper.
   Those numbers were chosen to be safe, not to be audible. Delay is one-shot
   with zero feedback by design, which will sound much weaker than an Echo to
   anyone expecting a repeat.

### Then correct

Only after the above says which stage is losing the signal:

- if the mapping is the problem, widen the wet/feedback ranges and re-taper,
  keeping the limiter downstream honest;
- if the DSP is the problem, fix it with a host test that asserts the wet
  signal's presence, not just that the code runs;
- if beat timing is wrong, Delay's known constraints apply — it samples
  effective BPM only when Beat FX state is applied, caps at 1000 ms, falls back
  to 120 BPM outside 40-300, and derives `1&2` timing from Deck 1.

### Acceptance

- a recorded `.wav` per effect and target showing a clear, controllable change
  from depth 0 to full;
- audible on the physical MAIN output, confirmed by the operator, at settings a
  DJ would actually use rather than only at extremes;
- no new clicks at ON/OFF/CLEAR or during beat-size changes, and Echo's ~2 s
  tail and Delay's previous-period tail still behave;
- host tests extended so a silently-inaudible effect fails in CI rather than on
  stage;
- `DOCUMENTATION_STATUS.md` updated to record a real acceptance instead of the
  standing "smoke pending".

## TODO: Small Open Items Found 2026-07-24

Three things surfaced while fixing the loop and shelving the recorder. Their
current disposition is recorded here rather than leaving stale TODOs.

### Dead PDB rows are offered in the library and fail on load

Indices 0, 1, 2 and 5 on the current USB stick — `sample-15s.wav`,
`Sample_BeeMoved_96kHz24bit.flac`, `file_example_WAV_10MG.wav`,
`Symphony No.6 (1st movement).flac` — are rekordbox database rows with no file
behind them. Loading one returns `AUDIO_LOAD_FAILED a1=261 NOT FOUND` and
leaves the deck in `ERROR`.

They cost a real diagnosis this session: every non-mp3 entry in the library is
one of these, so a load failure was briefly read as "FLAC is broken" when FLAC
is fine. **Operator decision 2026-07-26: retain these rows unchanged as a
repeatable corrupt/missing-media test fixture.** The expected result remains a
bounded `NOT FOUND` load failure and recoverable deck state; do not silently
filter this particular test database.

### Underrun at track start

`AUDIO_UNDERRUN a0=512` fires shortly after a track starts playing, before any
loop is armed. Separate from the loop-trim regression fixed in
`RC1-232-g8f6656cb`. Software remediation on 2026-07-26 adds a 512-frame
startup gate: PLAY can be latched while the producer fills, but the output
mixer does not consume the deck until two 256-frame blocks are available (or a
real short EOF tail exists). `/api/status` now reports
`startup_waiting1/2`, `startup_wait_count1/2` and
`startup_prebuffer_frames` alongside `pcm_underrun1/2`. Host policy coverage
passes; hardware confirmation that the initial counter remains zero is pending.

### Bench power BROWNOUT / POWERON — recurred on experimental dual USB

`boot=84 reset=BROWNOUT` then `boot=85 reset=POWERON` on 2026-07-24, and the
FLX4 stopped enumerating until it was physically reconnected. The later bench
session traced this to the power supply and replacement resolved it for the
master topology. The experimental P4 dual-USB bench reproduced a raw
`BROWNOUT` on 2026-08-12: one deck ran 33 seconds cleanly, while two decks reset
after about 6.5 seconds even with FLX4 disconnected and zero reported audio
late blocks or underruns. This is an open power/hardware blocker for
`feat/p4-dual-usb-host`, not evidence of an audio scheduling fix. Measure and
stabilise the common 5 V/VBUS path, rebuild the restored monitor/cue source and
repeat the complete hardware matrix before merge.

### Experimental dual-USB software closure, 2026-08-27

The reusable M3 slices selected for this branch are implemented and
software-verified: fail-closed USB FIFO sizing; direct FLX4 MIDI plus
four-channel UAC; stateful 48→44.1 kHz resampling and bounded ring correction;
hotplug generations and task-priority transitions; FLX4-only shifted LED
mirrors; headphone level ramps; fractional/default/large Beat Jump pages; jog
loop-boundary editing; gapless slip-reverse Censor; and bounded UAC health
alarms with a playback-start baseline. The direct audio path has no S3 monitor
fallback: if USB1 UAC is unavailable, cue output is unavailable until direct
UAC recovers. The P4 host suite and ESP-IDF 6.0.2 P4-local build pass.

### Focused dual-root hotplug acceptance, 2026-08-29

The first physical reconnect pass after S3 retirement is complete. The accepted
source (`77aa23a`) adds indexed idle-only root recovery, suppresses a power
cycle once attach/enumeration is active, retires MSC callback ownership before
teardown, and keeps one fixed 8 KiB DMA-capable MSC transfer for the complete
device lifetime. FatFs transactions are split to the same 8 KiB bound. This
preserves the disconnect-race fix while avoiding the contiguous 32 KiB
internal-DMA allocation that returned `ESP_ERR_NO_MEM` after hotplug.

The complete P4 host suite and ESP-IDF 6.0.2 `build_signed` passed. The signed
`RC2-106-gfa55e43-dirty` bundle booted from `ota_0` with USB0 storage and USB1
FLX4 active. One physical USB0 remove/reinsert cycle completed without reboot;
the service journal recorded `USB_UNMOUNTED`, `USB_MOUNTED` and
`LIBRARY_LOADED`, while status reported 2/2 successful mounts, clean
unmount/uninstall, zero host/recovery failures and an active FLX4 MIDI/UAC
profile. Two later track loads completed from the remounted medium. See
`validation/P4_DUAL_USB_HOTPLUG_OTA_SMOKE_20260829.md`.

This closes the focused hotplug reproduction, not the release gate. Electrical
qualification, repeated cold/warm boots and insertion orders, removal during
active decode, repeated USB1 reconnect, 30-minute combined-load diagnostics and
the later multi-hour soak remain open.

### Bounded USB1 controller fault recovery, 2026-09-01

Commit `269036b` closes the observed high-rate controller recovery-request
storm. The first MIDI transfer/submit or direct-UAC fault opens one epoch,
stops new output, retires endpoint callbacks and tears down ownership before at
most one deferred USB1 recovery request. Duplicate reports in the epoch are
coalesced, and a physical device-gone event cancels the pending soft request.
The controller task also observes a latched UAC fault, eliminating the previous
false-active state with a stopped consumer.

The complete P4 host suite passed with the new pure-C recovery-gate test. A
clean ESP-IDF 6.0.2 build from exact commit `269036b` produced
`RC2-109-g269036b` (2,450,656 bytes; SHA-256
`7776f287f9f795abeee36ac648dc518517ec270823928293bf0b04cb334cd9ee`).
The signed candidate booted `ota_0`, mounted a 100-track USB0 Library and
activated direct FLX4 MIDI/UAC. Idle, 30-second dual-deck audio, one physical
USB1 reconnect and 20-second post-reconnect dual-deck audio completed with zero
new recovery requests, late blocks, UAC drops/overflow, PCM underruns or daemon
errors. USB0 remained at one mount and zero disconnects. See
`validation/P4_USB1_FAULT_RECOVERY_OTA_SMOKE_20260901.md`.

This closes only the focused controller-storm reproduction and one reconnect.
The repeated USB0/USB1 matrix, removal during decode, 30-minute/multi-hour soak
and measured protected-VBUS qualification remain open.

## Idle Screensaver

Status: **implemented and hardware-accepted 2026-07-24** in
`RC1-237-g7bf0fd3c`. Steps 1-4 shipped; step 5 was dropped by decision, see
below.

### Outcome

Operator confirmed all four behaviours on hardware: it appears after two idle
minutes, touch dismisses it, an FLX4 button dismisses it without acting on the
deck, and it never appears while a deck plays.

Two implementation points were not obvious from the plan and are worth keeping:

- **Consuming the waking event splits in two.** Touch is free — the screensaver
  is its own LVGL screen with no widgets, so a dismissing tap cannot reach what
  is underneath. Controller events are not, because they all pass through
  `deck_core_queue_event()`; that callback returns `bool` and the event is
  swallowed when it woke the screen, which is what stops a PLAY press from also
  starting the deck.
- **Restoring the screen is not enough.** LVGL repaints the whole tab on return
  and erases the direct-PPA waveform strips, exactly as a tab switch does.
  `ui_overview_note_screen_restored()` forces the existing tab-return recovery
  rather than adding a second one.

The caption ships at 24 px / `0xB0B0B0`; 16 px at `0x808080` was reported as
barely visible at the distance the panel is actually read from. It is static on
purpose — the wordmark already animates nine labels and this panel is sensitive
to the invalidate budget.

### Step 5 (Settings timeout entry) — dropped, not forgotten

The plan argued the timeout should be configurable from the start because "the
right value is a taste question and will be argued about". It was not argued
about: the operator settled on two minutes and explicitly declined the control
("ne treba mi kontrola timeouta jer želim da bude 2 minute").

So `UI_IDLE_DEFAULT_TIMEOUT_MS` in `ui.c` stays a compile-time constant. The
timing core (`ui_idle.c`) already takes the timeout as a parameter and supports
an Off position at 0, so adding the Settings entry later is wiring only — no
redesign. **Do not re-add it as an oversight; it was a decision.**

### Original plan (kept for reference)

Status: planned 2026-07-23.

Goal: after two minutes with no deck playing and no operator input, replace the
UI with the looping splash animation, with `press any button or don't...` along
the bottom. Any touch or any controller button dismisses it instantly and
restores the previous tab.

### What already exists

`components/ui/splash_screen.c` already renders "Pajoniiir" in the Musieer_80
font with a continuous fade animation, and `splash_screen_show(cb)` runs it for
three seconds at boot before handing off to the main UI. The screensaver is
that same animation without the timeout, plus a caption — not a new visual.

### Design

**Idle definition.** Idle requires *both* conditions: no deck playing
(`deck_core_get_state().playing` false for both) and no input for the timeout.
A paused deck with a track loaded still counts as idle; a playing deck never
does, however long nobody touches anything. Recording must also inhibit it —
blanking the UI mid-capture would be alarming even though it is harmless.

**Activity sources.** Two, and both must reset the timer:
- touch, from the LVGL indev read callback in `ui_lvgl_backend.c`;
- any controller event, which all pass through `deck_core_queue_event()` —
  a single hook there covers every FLX4 button, jog, fader and the web API.

Feed both into one `ui_activity_notice()` so the timeout logic has a single
input and can be host-tested.

**Dismissal.** The screensaver consumes the first event that wakes it: a touch
that dismisses must not also hit whatever button was underneath, and a FLX4
PLAY press should wake the screen without starting playback. This is the part
most likely to be got wrong and deserves an explicit test.

**Restore.** Return to the tab that was active, not to Overview. The Overview
waveform uses the direct PPA overlay path, so returning to it must re-arm the
strip reblit exactly as tab switching already does (`s_overview_prev_tab`),
otherwise the waveform comes back blank.

### Implementation order

1. Add `ui_activity_notice()` and a pure idle-timeout helper (elapsed, playing,
   recording in; show/hide out). Host-test it, including that a playing deck
   never idles and that recording inhibits.
2. Hook touch and `deck_core_queue_event()`.
3. Extend `splash_screen` with a loop mode and the caption, keeping the
   existing three-second boot behaviour unchanged.
4. Wire show/dismiss into `ui.c` with correct tab restore and first-event
   consumption.
5. Settings entry: timeout in minutes with an Off position, persisted through
   `app_settings` like backlight is.

### Notes and risks

- Two minutes is the default, not a constant: make it configurable from the
  start, since the right value is a taste question and will be argued about.
- Consider dimming the backlight alongside, but keep it a separate decision —
  `bsp_display_set_backlight()` already exists and the panel is the main power
  draw. Do not couple the two until the screensaver itself is accepted.
- The LVGL invalidate budget is delicate on this panel; the animation must not
  reintroduce the full-screen redraws that the 2026-07-09 stability pass
  removed. Watch for DSI underruns while it runs.

### Acceptance

- idles after the configured time with decks paused, and never while a deck
  plays or a recording runs;
- touch and any FLX4 button both dismiss it, and neither action leaks through
  to the control underneath;
- returns to the tab that was active, with the Overview waveform intact;
- no DSI underrun, watchdog or audio disturbance while it runs — verify with a
  deck paused mid-track and the recorder idle, then again immediately after
  dismissal;
- setting survives a reboot.

## Deferred Phase: Native Folder Library And libapta-audio 1.1

Status: **planned for after the upstream libapta-audio `v1.1.0` release; not
implemented on current `master`.**

The future P4 library path will accept ordinary MP3/WAV/FLAC folders, maintain a
compact 10,000-track catalog, store versioned `/PAJONIIIR` sidecars and use
libapta for bounded progressive waveform/tempo/grid/meter/key analysis.
Rekordbox PDB/ANLZ remains supported as an importer, while application consumers
migrate from `anlz_metadata_t` to a P4-owned immutable `track_analysis_t` model.

The canonical ordered plan, memory limits, USB transaction rules, rollout flag,
test matrix and completion gate are in
[`LIBAPTA_P4_INTEGRATION_PLAN.md`](LIBAPTA_P4_INTEGRATION_PLAN.md). Do not start
the production integration from the upstream development branch: first require
the tagged 1.1 release, exact commit pin, reproducible `dependencies.lock`,
algorithm corpus gates and ESP32-P4 memory evidence.
