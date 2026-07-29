# Startup Checklist

Status: reconciled 2026-07-28. Checked items below are historical bring-up
evidence, not instructions to repeat old commit-specific flashes. The latest
clean release build is newer than the installed firmware.

## Current installed and accepted baselines

- [x] Latest clean dual-target release build: `RC1-259-gdaf4639`; both
  `build_signed` targets were rebuilt with ESP-IDF 5.5.4 on 2026-07-26. The
  candidate includes the Pajoniiir rebrand, synchronized simulator support,
  OTA/controller/startup hardening and has not yet been signed, packaged or
  installed. Exact raw-image sizes and SHA-256 values are recorded in
  `validation/CLEAN_RELEASE_RC1_259_BUILD.md`.
- [x] ESP-IDF v6.0.2 Migration: Svi softverski buildovi i host testovi za P4 i S3 u potpunosti prolaze pod ESP-IDF v6.0.2 na dan 2026-07-28 (grana `migration/esp-idf-6.0.2`). Integrirani su bounded compressed audio cache (8 × 32 KiB LRU po decku), paginirana Library tablica (8 redaka s PREV/NEXT), immutable track sort, recorder safety hardening i stabilizacijski popravci iz `fix/release-blockers-and-concurrency`.
- [x] Last known matching bench baseline: P4 and S3 both
  `RC1-254-g21f21963` as of 2026-07-24.
- [x] P4 pull OTA is hardware-proven end to end: temporary STA visit, HTTPS
  channel read, signed bundle download/verification, inactive-slot flash and
  reboot.
- [x] Latest fully functionally accepted P4/S3 release:
  `RC1-123-g587cd7a1` (acceptance-time slots on 2026-07-14: P4 `ota_0`, S3
  `ota_1`). Later builds have extensive focused acceptance but have not yet
  replaced this complete-system baseline.
- [x] P4 and S3 OTA success, interruption safety and forced rollback accepted.
- [x] Vinyl/scratch accepted on both platters.
- [x] Master Tempo basic hardware behavior accepted.
- [x] Deterministic five-minute PC dual-deck Master Tempo soak: 14,400,000
  output frames per deck, zero source-position drift, zero detected clicks,
  zero per-deck/mixed clipping and finite rebased DSP state. This is host
  regression evidence, not P4 CPU/deadline or listening acceptance.
- [x] PCM5102A MAIN and FLX4 USB headphone cue operate together.
- [x] R1 EOF drain/replay implementation passes P4 host tests and firmware build.
- [x] R1 basic hardware smoke: full track tail plays and PLAY restarts after natural EOF.
- [x] Dual-deck MT watchdog fix host/build-verified and 45-second serial-smoked without WDT/drop.
- [x] R2 PCM timeline race and missing-source seek guard pass P4 host tests and firmware build.
- [x] R2 scratch-freeze writer timeout fix passes P4 host tests and firmware build.
- [x] R2 USB DWC BNA/CHHLTD compatibility wrapper passes link, host and P4 build checks.
- [x] R3 lossless priority-touch policy passes both host suites and firmware builds.
- [x] R4 WPA2 AP, OTA finish-state and signed-version packaging fixes pass both host suites and firmware builds.
- [x] R5A call-graph, signed-build size and legacy S3 build baselines recorded.
- [x] Full code-review software remediation passes both host suites, OTA signing,
  OTA release helpers and clean ESP-IDF v5.5 builds for both targets.
- [x] Clean `RC1-133-gbd5e43ce` P4/S3 packaging builds, both signed bundles and
  the outer manifest verified with `rel-001`; only the exact P4 payload was
  wired-flashed, while S3 remained installed on RC1-131.

## Repeat before enclosure close

- [ ] Verify shared ground and that independent 5 V sources are not back-fed.
- [ ] Verify UART and PCM-link wiring against `HARDWARE_WIRING.md`.
- [x] Run both host suites and both firmware builds from fresh build directories —
  passed 2026-07-26 with ESP-IDF v5.5.4; `migration/esp-idf-6.0.2` passed
  2026-07-28 with ESP-IDF v6.0.2 including bounded cache, paginated Library
  and recorder hardening. Repeat for the final enclosure candidate.
- [ ] Hardware-validate bounded compressed cache under dual-deck load with real
  MP3/WAV/FLAC files.
- [ ] Hardware-validate paginated Library table on the P4 touch display.
- [ ] Perform a long dual-deck audio/vinyl/key-lock soak.
- [ ] Extend R1 smoke to both decks with Master Tempo off/on and near-EOF scratch/hold.
- [x] R2 basic smoke: dual-deck playback/scratch capture has no writer timeout, fallback or PCM drop.
- [x] R2 USB smoke: 30-second playback/storage capture has no DWC assert, reboot or media loss.
- [x] R3 smoke after flashing both targets: dual-platter scratch/release operates correctly without a latched platter.
- [x] R4 smoke: both WPA2 APs accept `Pajoniiir`; P4 web UI plus S3 log and OTA update pages load correctly.
- [x] Repeat a 45-second dual-deck MT serial capture and confirm no `IDLE0` task watchdog.
- [ ] Measure enclosure temperature and check RF/AP reachability.
- [x] Perform one OTA update per target and record slot/version/state; the
  2026-07-16 `RC1-131-gc391e306` rollout is recorded below.
- [ ] Preserve a wired recovery path or validated service connector.
- [ ] Run Phase 20 hardware acceptance: dual-deck DSP/FX soak, FLX4 USB
  disconnect recovery, guarded web/profile/OTA mutations and UART-link capture.

## Repository

- [x] Add `docs/reference/Pioneer-DDJ-FLX4.midi.xml`.
- [x] Commit the baseline import and Pajoniiir documentation.

## Local Tooling

- [x] Confirm ESP-IDF v6.0.2 is installed (migrated from v5.5.4).
- [x] Confirm v6.0.2 PowerShell profile init script works in PowerShell.
- [x] Confirm `idf.py --version`.
- [x] Confirm MinGW/GCC is available for PC tests (msys64 ucrt64).
- [x] Use `tests/run_s3_host_tests.ps1` for S3 host regressions (adapted for PowerShell 5.1 compatibility).
- [x] Use `tests/run_p4_host_tests.ps1` for P4 host regressions (runs on both Windows PowerShell 5.1 and PowerShell 7).

## Baseline Builds

- [x] Build `firmware/control-board-s3`.
- [x] Build `firmware/main-deck-p4`.
- [x] Run inherited PC tests that do not require hardware.

## Hardware Bring-Up

- [x] Confirm S3 serial port (`COM3` on 2026-06-08).
- [x] Confirm P4 serial port (`COM15` on 2026-06-13).
- [x] Flash S3 FLX4 host-mode firmware (`fd663e6`) before FLX4 capture.
- [x] Flash P4 firmware after dual-deck UI stabilization (`5f9b425` on 2026-06-13).
- [x] Verify S3/P4 UART heartbeat.
- [x] Validate DDJ-FLX4 physical USB host setup on S3.
- [x] Capture raw MIDI packets for MVP controls.

## Current Repository State

- `master` includes the P4 dual-deck UI refactor, the 2026-06-13 Deck 2
  Overview waveform jitter fix, the S3 review fixes for FLX4 host/translator
  mode, the enabled S3 UART translation configuration, FLX4 reconnect LED
  resynchronization, raw Smart CFX/Smart Fader input mapping, the P4 splash
  screen port, the official DDJ-FLX4 MIDI message list, and the merged Phase 7
  extended-control surface.
- Phase 7 was merged into `master` and pushed on 2026-06-26. Completed stale
  Codex branches were removed locally and remotely after the merge.
- The old experimental `codex/flx4-extended-controls` was reviewed and removed
  (local + remote) on 2026-07-03 after confirming its verified slices were
  already salvaged into `master`. This is a dated cleanup snapshot, not a
  permanent invariant. A 2026-07-20 audit then retired the last branch:
  `codex/phase-8-implementation` was confirmed superseded by `master`, its
  unique GPIO48 RGB status-LED policy engine archived under tag
  `attic/phase-8-status-led-policy`, and its branch + worktree removed. All
  merged branches were pruned local + remote, leaving only `master`, and the
  repository moved to `https://github.com/dvucinozd/Pajoniiir.git`. A follow-up
  audit on 2026-07-26 removed five fully merged remote maintenance branches and
  two fully merged stale local branches, each with 0 unique commits; only
  `master` remains locally and on `origin`. Still inspect reachability and
  archive unique work under `attic/*` before deleting any future branch.
- The former `codex/p4-review-fixes` scope is merged: per-deck audio status,
  shared output/codec lifecycle, deck-core lock scope cleanup, high-rate
  control coalescing, source-safe media load, parser hardening, and the P4 host
  regression runner are now part of `master`.
- S3 review fixes include a host regression runner, hardened DDJ-FLX4 USB MIDI
  descriptor handling, deck-aware S3 `control_link` constants, an XML-derived
  FLX4 MIDI mapper, translator-mode UART coalescing, and safer legacy CDJ panel
  queue behavior.
- P4 Overview waveform path includes RGB565 circular-strip scrolling:
  steady main waveform motion should report `UI_OVERVIEW_WAVE_CACHE_OFFSET`
  with zero rendered columns, while occasional edge updates render bounded
  batches instead of moving the whole waveform buffer. With UI diagnostics
  enabled, the Overview cache log reports cumulative `FULL`, `OFFSET`, `EDGE`,
  and `NONE` counts plus total rendered columns/blits.
- Current Overview waveform load path defers main-waveform render/blit to the
  Overview scheduler and briefly reblits both deck overlays after any track
  load. 2026-07-01 hardware smoke confirmed that Deck 1 waveform appears after
  load without touching the screen, Deck 2 load does not blank Deck 1, both
  waveforms remain visible, and Browse-rotate zoom uses the shared
  4/8/12/16/24-beat steps.
- Current P4 Overview polish keeps title/timer LVGL invalidation bounded,
  uses a compact blue-strip remaining-time pill, keeps BPM and pitch readable,
  centers beat-match/phase indicators around the main playhead, uses compact
  D1/D2 deck badges, separates deck VU meters from Play/Cue touch targets, and
  renders Beat FX as a compact right rail.
- P4 UI Phase 6 is closed for the local touchscreen path: `ui.c` is now an
  887-line orchestrator, with Overview, Library, Controls, Performance tabs,
  Settings, Status, LVGL backend, renderer, scheduler, and frame-context logic
  split into focused modules.
- S3 DDJ-FLX4 raw MIDI capture and translation are verified and completed.
- S3 and P4 Phase 5 LED feedback (Play, Cue, PFL) is verified and completed.
- S3 publishes FLX4 USB connection state and P4 forces a complete MVP
  Play/Cue/PFL LED snapshot after reconnect; hardware verification passed on
  2026-06-20.
- SMART CFX and SMART FADER raw inputs are mapped as momentary semantic
  press/release events. P4 owns their toggle state, LED feedback, status
  exposure, Smart CFX filter DSP with a softened raw/effective macro curve,
  balanced HI-side filter behavior verified on hardware, and Smart Fader
  transition-assist behavior. Smart Fader hardware smoke passed on 2026-07-01.
- Trim/pregain routes the FLX4 deck-local Trim knobs into P4 audio output gain
  as a bounded pregain scalar: center is unity, left attenuates, and right
  boosts up to +6 dB before the existing post-sum limiter. Host tests cover the
  audio gain curve, mixer snapshot, `/api/status` exposure, and deck_core
  routing. Hardware smoke on 2026-07-01 confirmed both deck Trim knobs
  attenuate below center and boost above center after allowing pregain gain
  through the output mixer before the master limiter.
- Master Level is mapped from the official FLX4 MIDI PDF as `0xB6/0x08+0x28`.
  S3 forwards it as `CTRL_ID_MASTER_VOLUME`, and P4 applies it as a runtime
  non-boosting master volume before the existing persistent Settings master
  trim. Host tests cover the mapping, shared control-link ID, deck_core routing,
  audio gain path, and `/api/status.mixer.master_volume`. Hardware smoke on
  2026-07-01 confirmed full-range control from mute/low level through normal
  master output.
- Beat FX section mapping and P4-owned state are implemented for effect select,
  beat size, target, depth, on/off, and clear/reset. Beat FX FILTER audio DSP is
  implemented as a target-aware low-pass slice. Beat FX Echo has a beat-time
  DSP slice with P4-owned delay buffers, target-aware routing, and
  `/api/status.diagnostics.beat_fx_echo` telemetry; delay time is derived from
  target deck effective BPM when Beat FX state is applied, with a 120 BPM
  fallback and 1000 ms cap. Later tempo/track changes do not automatically
  retime the active line. The
  Overview Beat FX rail renders the same P4-owned state with compact active
  status, target/effect/depth readouts, and reduced overlap risk, and
  `/api/status.beat_fx` remains available for hardware smoke verification
  instead of raw serial logging when a network transport is present. Hardware
  smoke passed on 2026-07-01 for the Beat FX FILTER and Echo behavior, gradual
  depth response, CH1/CH2/1&2 target routing, beat-derived Echo beat-size
  changes, and physical ON/OFF LED feedback.
- **2026-07-10 update:** the Beat FX audio DSP was reworked for better sound and
  less touchy knobs — resonant state-variable channel/Beat-FX filter with an
  exponential kill sweep, tape-style Echo with feedback damping + ring-out tail,
  and a smoothstep Smart CFX curve — and a beat-derived **Flanger** was added as a
  third Beat FX effect (cycle FILTER → ECHO → FLANGER). The Overview Beat FX rail
  is now an effect-colour-coded strip with a vertical depth meter. Sound is now
  the default build on both boards (`idf.py build`); the per-profile sdkconfig
  overlays were removed.
- **2026-07-16 update:** Beat FX **DELAY** value `4` is implemented as a
  beat-sized, full-band one-shot repeat with zero feedback; Level/Depth controls
  wet gain. ECHO remains the damped multi-repeat effect. Both modes share the
  existing per-deck stereo delay line without another PSRAM allocation. Delay
  time samples effective BPM when Beat FX state is applied, uses only the
  40–300 BPM range, falls back to 120 BPM, is capped at 1000 ms, and uses Deck
  1 BPM for target `1&2`. A later tempo, Beat Sync or track-load change does not
  automatically retime it until another Beat FX event republishes the state.
  The selector is
  `FILTER → ECHO → FLANGER → DELAY → FILTER`, Previous runs in reverse, and
  `NONE=0` remains an unselectable compatibility sentinel. CLEAR restores
  disabled FILTER, beat size 1, target `1&2` and depth 64. P4 host suites,
  signed builds and OTA deployment provide software/release acceptance;
  focused FLANGER and DELAY hardware smoke is still **PENDING**.
- Pad FX DSP first slice is implemented in P4 and host-tested through
  `CTRL_PAD_ACTION` events for PAD_FX1/PAD_FX2. Physical FLX4 Pad FX pad input
  mapping is implemented from the official MIDI message PDF (`0x10..0x17` for
  Pad FX1 and `0x50..0x57` for Pad FX2 on `0x97`/`0x99` deck pad statuses);
  2026-07-01 smoke confirmed filter pads and Echo routing. Short Echo presses
  keep a host-tested release tail instead of clearing the delay buffer
  immediately. Pad FX1/Pad FX2 normal pad LEDs are implemented as P4-owned
  momentary press feedback and host-tested from the official PDF; hardware LED
  smoke passed on 2026-07-01.
- Official DDJ-FLX4 MIDI message list coverage is documented in
  `docs/reference/DDJ-FLX4_MIDI_message_List.md` and cross-referenced from
  `docs/DDJ_FLX4_MIDI_MAP.md`; the Mixxx XML remains the proven authoritative
  input source.
- P4 dual-deck audio scheduling is hardware-verified after the 2026-06-20
  preload/output pacing pass: both decks can play with normal audio and normal
  waveform motion.
- USB library import handles FAT32 and exFAT on superfloppy, MBR, and GPT
  layouts via `usb_media_mount` (base-LBA translation + vendored FatFs with
  `FF_FS_EXFAT=1`). exFAT large sequential reads are chunked into ≤64-sector SCSI
  commands (exFAT's large clusters otherwise exceed the MSC bulk transfer limit
  and silently abort the audio preload). Hardware-verified: MP3/WAV/hi-res FLAC
  all play from an exFAT drive.
- P4 master output now uses a transparent soft-knee post-sum limiter with
  lightweight limiter telemetry in both the output diagnostic log and the audio
  mixer snapshot. Material below roughly ±30000 PCM units remains unchanged;
  hotter post-sum peaks are compressed toward the int16 ceiling instead of
  hard-clipped. The P4 status indicator briefly shows `CLIP n` when the limiter
  counter increases.
- 2026-06-30 dual-deck RCA smoke on commit `a6cbae8` passed for audio: Deck 1
  alone OK, Deck 1 + Deck 2 OK, RCA audio OK, and `CLIP` only occasional. The
  COM15 capture `logs/p4_dual_deck_soft_limiter_smoke_20260630_214519.log`
  reported `active=1/1`, `late=0 late_max=0 us`, healthy rings, and limiter
  counts only during hot summed peaks. Waveform stutter was still observed and
  remains the next UI/render performance item.
- Follow-up waveform scheduling testing showed that capping Overview main
  waveform redraw to one deck per UI tick did not resolve the visible stutter
  once the audio path was otherwise healthy. The scheduler now allows both
  playing deck waveforms to redraw in the same UI tick so each deck keeps full
  visual cadence; the remaining 2026-07-01 hardware issue was traced to mixed
  44.1/48 kHz deck playback and fixed in the audio resampler path.
- S3 USB MIDI host responsiveness was hardware-verified on 2026-06-21 after
  FLX4 VU feedback was made low-priority under USB MIDI OUT queue backlog and
  raw USB MIDI packet logs were demoted to DEBUG in translator mode. Both
  decks can play while controller Play/Pause remains responsive.
- S3 extended LED snapshot recovery was fixed on 2026-06-26 after hardware
  smoke exposed a `ctrl_rx` stack overflow during the wider Phase 7 forced LED
  snapshot. The MIDI OUT queue now covers the full non-VU snapshot burst,
  full-queue warnings are rate-limited, and `ctrl_rx` has a 4096-byte stack.
  Post-fix S3 reset recovery re-enumerated FLX4 and the operator confirmed the
  controller was responsive. Full manual FLX4 USB replug also restored the
  P4-owned LED state without an S3 reboot loop.
- P4 audio output diagnostics were calibrated on 2026-06-21: normal blocking
  `esp_codec_dev_write()` pacing no longer emits per-block `diag output late`
  warnings. A dual-deck hardware run reported zero late warnings while keeping
  aggregate output, limiter, heap, internal SRAM, and PSRAM telemetry. The
  audio engine also exposes these values through a central diagnostics snapshot,
  and `/api/status` mirrors them under `diagnostics` for smoke captures.
- P4 PCM5102A MAIN OUT bring-up passed a 2026-06-27 COM15 measurement with the
  external DAC enabled locally. The PCM5102A I2S1 clock is now reconfigured to
  the loaded track sample rate when the shared output service opens, audio
  loader/decode/output tasks run on CPU0, LVGL remains on CPU1, and a
  dual-deck run reported `late=0 late_max=0 us` with stable ring fill. The
  local `firmware/main-deck-p4/sdkconfig` used for this hardware test is
  intentionally ignored and should not be committed.
- PCM5102A final output acceptance passed on 2026-06-30: the photographed
  PCM5102MK/PCM5102A board produced audio from both RCA and its onboard 3.5 mm
  output. The matching COM15 smoke capture is
  `logs/p4_pcm5102a_rca_smoke_20260630_123632.log`, after boot probe
  `logs/p4_pcm5102a_boot_probe_20260630_123558.log`. Next audio work should
  focus on gain staging and limiter polish rather than DAC bring-up.
- P4 audio engine now exposes a non-boosting software master trim API and
  mixer snapshot field. The Settings tab has a preset button cycling `0 dB`,
  `-3 dB`, and `-6 dB`. Default remains unity, so current playback level is
  unchanged until the operator deliberately lowers it. The selected preset is
  persisted through NVS and reapplied during P4 boot after `audio_engine_init()`.
- Mixed sample-rate dual-deck playback is fixed as of 2026-07-01. A hardware
  repro showed that France Gall + Comanchero were both 44.1 kHz and played with
  fluid waveforms, while Men At Work + Caribbean Blue mixed 44.1 kHz and
  48 kHz and previously caused waveform stutter. The output mixer now applies
  each deck's `source_sample_rate / output_sample_rate` ratio on top of pitch,
  and hardware smoke confirmed audio OK, fluid waveform, normal Caribbean Blue
  playback, and no reboot.
- P4 firmware defaults now select performance optimization and disable LVGL
  examples/demos. If an ignored local `firmware/main-deck-p4/sdkconfig`
  predates 2026-06-25, regenerate or align it before flashing so it does not
  keep `CONFIG_COMPILER_OPTIMIZATION_DEBUG`.
- P4 now includes the ported LVGL splash screen from the former
  `codex/splash-screen` branch. Boot shows `Pajoniiir` in `Musieer_80` for
  roughly three seconds, then returns to the already-built main dual-deck UI.
  The `ctrl_rx` UART task stack is 4096 bytes in the same stabilization slice.
- ESP-Hosted Wi-Fi was re-enabled on 2026-07-04 behind a Settings switch
  (`app_settings.wifi_remote`, default **off**). The onboard ESP32-C6 provides a
  SoftAP `Pajoniiir` over SDIO; turning the switch on runs `wifi_link_start()`
  (hosted + Wi-Fi + `web_server` + captive DNS) and off runs `wifi_link_stop()`
  (full teardown, incl. `esp_hosted_deinit`). HTTP status and captive DNS startup
  are still gated behind successful Wi-Fi/AP init because `esp_http_server`
  asserts in lwIP if started before the TCP/IP stack. The mobile web controller
  is at `http://192.168.4.1`. The old Settings `link_mode`/`JOINED` selectors
  remain removed. (Historical: it was parked 2026-06-29 as a no-op shim for
  RF-quiet development.)
- A separate runtime **S3 Debug AP** was added and merged to `master`
  (`CONFIG_S3_DEBUG_AP_ENABLED=y`, default). It is independent of the P4
  Wi-Fi remote: the S3 hosts its own WPA2 SoftAP `Pajoniiir-S3-DEBUG` +
  read-only live log viewer at `http://192.168.4.1`, toggled from a
  non-persisted P4 Settings switch over `CTRL_ID_S3_DEBUG_AP` (`0x82`/`0x85`).
  OFF at every boot; P4 also sends OFF at boot as a safe reset. Host tests and
  both firmware builds pass; AP/live-log hardware smoke passed on 2026-07-08.
  See
  `docs/S3_WIFI_DEBUG_LOG.md` and `docs/CONTROL_LINK_PROTOCOL.md`.
- Settings UI polish on 2026-07-08 removed Key Shift presentation from active
  UI, removed the retired monitor-speaker switch, changed wireless switch off
  states to stay dark/non-white, and replaced the lower mixer/PFL routing block
  with a compact status strip.
- FLX4 USB headphone product streaming was stabilized on 2026-07-09 after
  intermittent S3 `p4_audio_link` overruns were traced to a post-start
  sample-rate mismatch between the P4 monitor PCM producer and the FLX4 USB
  Audio consumer. S3 now tracks the active P4 link rate while already in ring
  mode, reapplies the FLX4 endpoint rate when supported, and reinitializes the
  USB packetizer. COM6 smoke after flashing showed `overruns=0`, `gaps=0`,
  `crc=0`, and `FLX4_USB_AUDIO skipped=0 underrun=0` for roughly two minutes.

## First Firmware Task

`firmware/control-board-s3/components/flx4_midi_host/` contains the raw
USB MIDI logger and the software translator path. Built with
`CONFIG_DDJ_FLX4_TRANSLATE_TO_P4=y` (enabled on 2026-06-14). USB host role is
unconditional since R5D; disabling the translator leaves the raw logger.

## P4 Overview Waveform Smoke Test

- [x] Flash current P4 firmware to COM15. Last confirmed: 2026-07-17 with the
  factory-slot exact payload from signed candidate `RC1-133-gbd5e43ce` after
  the DSI-synchronised waveform fix.
- [x] Load Deck 1 from Library and confirm the main waveform appears without
  touching the screen.
- [x] Load Deck 2 and confirm the Deck 2 waveform appears while the Deck 1
  waveform remains visible.
- [x] Confirm Browse rotate changes both main Overview waveforms through the
  shared 4/8/12/16/24-beat zoom steps on the Overview tab.
- [x] Press Beat Sync with both decks loaded and confirm the Overview
  beat-match guide lines align after the one-shot seek.
- [x] Development A/B winner: approximately 132 seconds of dual-deck playback;
  fluid waveforms, no "underwater" motion or visible flash, zero DSI underruns
  or PCM drops, final `submitted=22882 sent=22882 dropped=0`.
- [x] Exact signed-candidate payload re-smoke: more than 71 seconds of active
  playback; no flash or jitter, zero DSI underruns or PCM drops and no panic,
  watchdog timeout, brownout or unexpected reset. Final sample:
  `submitted=13392 sent=13391 dropped=0`.
- [x] Temporary timing/cache diagnostics isolated the problem from audio and
  waveform-cache generation: both full-cadence RGB565 strips are PSRAM-backed,
  while one-deck scheduling reduced display pressure but caused the visible
  watery cadence. The retained fix is refresh synchronisation, not a reduced
  scheduler budget or GDMA QoS override.

Detailed A/B evidence and the exact acceptance boundary are recorded in
[`validation/P4_OVERVIEW_DSI_SYNC_SMOKE_20260717.md`](validation/P4_OVERVIEW_DSI_SYNC_SMOKE_20260717.md).

S3 status: USB host was successfully brought up on native OTG port. By increasing
`CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE=512`, the large configuration descriptors
of Pioneer DDJ-FLX4 are now successfully parsed. Raw MIDI capture of MVP controls was
verified to match `docs/DDJ_FLX4_MIDI_MAP.md`. Heartbeat and translator tasks are active,
emitting deck-aware `0xA5` control link frames.

Required output from the spike:

- FLX4 device descriptor summary: successfully verified.
- Endpoint/interface summary: interface=4, endpoint=0x82 (MIDIStreaming IN).
- Raw packet logs for every MVP control: verified (Play, Cue, Load, Browse, Faders, Pitch, PFL).
- Differences from `docs/DDJ_FLX4_MIDI_MAP.md`: none found, map is 100% accurate.

After the capture, `CONFIG_DDJ_FLX4_TRANSLATE_TO_P4` was enabled. S3 now successfully emits
deck-aware 7-byte `0xA5` frames while P4 heartbeat detection is supported.

## Next Controller Expansion

- [x] Browse press (`0x96/0x41`) is routed end to end as a P4
  Library/Overview toggle. Load 1 and Load 2 remain the only deck-load
  buttons, and Browse rotate moves the selected row one detent at a time.
  Hardware smoke on 2026-06-25 verified the Browse path. Since 2026-07-26 the
  `deck` task only queues browse/load commands; `ui_update()` drains them in
  the LVGL task (maximum eight per frame), so the old controller-triggered
  Library call chain no longer runs on the deck stack.
- [x] MVP Play/Cue/PFL LED reconnect resynchronization is routed end to end
  through S3 FLX4 connection state and P4 forced LED snapshots.
- [ ] Hardware smoke S3-to-P4 input snapshot replay: move Master Level, channel
  faders, crossfader, Trim, EQ, Filter/CFX, Headphones Mix, and Beat FX Depth;
  reboot only P4 while S3/FLX4 remain powered; confirm P4 reapplies the known
  values without moving the controls again. Tempo faders and discrete
  buttons/toggles are intentionally out of scope for this absolute-value smoke.
- [ ] Hardware smoke durable held-state reconciliation: under an injected
  stalled/slow P4 consumer, press and release Deck 1/2 JOG TOUCH, SHIFT, Censor,
  Pad FX 1/2 pads and shifted Beat Loop roll pads; confirm the final level always
  wins after recovery. Repeat with FLX4 disconnect while held and with a P4-only
  reboot while S3/FLX4 remain powered. Confirm no scratch/Shift/pad latch remains
  and record `/api/status` field `control_link.sequence_gaps` before and after
  each case.
- [x] SMART CFX (`0x96/0x00`) and SMART FADER (`0x96/0x01`) are raw-captured
  and mapped as semantic input-only button events.
- [ ] Hardware smoke Beat FX FLANGER/DELAY: select both in both directions and
  confirm there is no selectable `NONE` gap. For DELAY, verify one full-band
  repeat with no feedback regeneration, Level/Depth wet response, beat-size
  timing including the 1000 ms cap/fallback, CH1/CH2/1&2 target routing (current
  `1&2` timing follows Deck 1 BPM), and changes of beat size, tempo/Beat Sync
  and track while active. Verify OFF/CLEAR retains exactly the previous Delay
  period for its pending tap, Echo rings for about 2 s, and re-enabling a time
  effect or changing live ECHO↔DELAY mode clears stale shared-line content. A
  bounded old time-effect tail may briefly overlap a newly selected
  Filter/Flanger. Confirm CLEAR defaults (disabled FILTER, beat 1, `1&2`, depth
  64). For FLANGER, verify audible sweep, beat-size timing, Level/Depth response
  and all targets.
- [x] Build the extended control inventory from the vendored Mixxx XML.
- [x] Add deck modifiers and transport extensions with P4-owned semantics.
  First slice implemented: Shift, Cue+Shift track-start, Beat Sync, and
  Beat Sync+Shift tempo-range semantic inputs. Cue+Shift has P4 seek-to-start
  behavior; Beat Sync applies one-shot BPM match to the other deck using
  precise ANLZ BPM when available and an internal ±20% safe clamp independent
  of the selected manual Tempo Range. It phase-aligns to the nearest matching
  beat while preserving the reference deck's signed intra-beat offset when both
  beatgrids are available, including while the target deck is playing.
  2026-07-01 hardware smoke confirmed that Beat Sync aligns the Overview
  beat-match guide lines after the one-shot seek; it does not yet
  continuously follow. Tempo
  Range cycles deck-local `±6%`, `±10%`, and `±16%` fader ranges. Final Phase 7 smoke
  verified loop in/out, reloop/exit, loop halve/double, and beat-jump
  back/forward inputs on both decks. Loop In/Out, Reloop/Exit, loop
  halve/double, normal/shifted Beat Loop pads, and Beat Jump buttons/pads now
  have P4 behavior; Tempo Range hardware behavior smoke passed on 2026-06-25,
  normal and shifted Beat Loop hardware behavior smoke passed on 2026-07-01,
  and Beat Jump pad hardware behavior smoke passed on both decks on 2026-07-01.
  Shifted Jog Search is now implemented as a 1000 ms per-step deck seek from
  the XML mapping; hardware smoke passed on 2026-07-02. Browse+Shift
  rotate/press, Beat Sync master, Reloop/Exit+Shift stop, Loop Adjust In/Out,
  Quantize, and Play+Shift slip-censor MVP are implemented from the XML mapping
  with host-test coverage; hardware smoke remains pending for this shifted
  control group. The controller's MIDI Vinyl-mode output toggle remains outside
  the controller-feedback scope; this is separate from the implemented audible
  vinyl/scratch engine.
- [x] Add supported mixer/monitoring controls and 14-bit range tests.
  Second slice implemented: Trim, EQ high/mid/low, filter, headphone mix,
  loop/beat-jump buttons, pad modes/actions, and P4-driven FLX4 VU meter output
  are mapped/tested in firmware. Hardware capture status is tracked per row in
  `docs/DDJ_FLX4_MIDI_MAP.md`.
  Trim/pregain, three-band EQ, and Headphones Mix now have P4 DSP behavior.
  Filter is used by Smart CFX while enabled, with HI/LOW hardware smoke passed
  on 2026-07-01. Headphones Mix and Headphones Level now feed the FLX4 USB
  headphone path, with audible hardware smoke passed on 2026-07-07. MASTER CUE
  is implemented from the official MIDI list as a P4-owned monitor master-cue
  gate with LED feedback; hardware smoke passed on 2026-07-02.
- [x] Connect supported FLX4 pad mode inputs to P4-owned semantic pad mode
  state.
  Hot Cue, Pad FX1/2, Beat Jump, and Beat Loop are mapped where noted in the
  MIDI map. Keyboard/Stems, Sampler, and Key Shift were removed from product
  scope on 2026-07-07; S3 ignores those input ranges and P4 ignores stale/manual
  control-link events for those modes. Post-flash hardware smoke on 2026-07-07
  confirmed supported modes still work and unsupported modes stay inert. Hot Cue pad behavior is
  implemented in P4 for per-track store/recall and shifted clear; Deck 1
  hardware behavior smoke passed on 2026-06-21, and Deck 2 shifted clear smoke
  passed on 2026-06-26.
  Normal and shifted Beat Loop plus Beat Jump pad behavior is implemented in P4.
  Normal and shifted Beat Loop hardware behavior smoke passed on 2026-07-01;
  Beat Jump pad hardware behavior smoke passed on both decks on 2026-07-01.
  Pad FX has a
  host-tested P4 DSP slice and official-PDF-backed FLX4 Pad FX pad input
  mapping; hardware smoke passed on 2026-07-01 for pad behavior, Echo tail, and
  normal Pad FX pad LEDs.
- [ ] Expand LED feedback only from P4-confirmed state.
  First firmware slice is implemented for P4-owned selected pad mode LEDs
  across direct and shifted modes, Beat Sync enabled state, and
  Loop In/Out LEDs derived from P4 pending loop-in marker and active audio loop
  state. Hot Cue normal pad LED output is implemented from P4 hot-cue-store
  slot state while Hot Cue mode is selected; hardware LED smoke passed on
  2026-07-01.
  Beat Loop normal pad LED output is implemented from P4-owned active Beat Loop
  pad state plus selected Beat Loop pad mode; shifted mirror pad LED output
  remains deferred. A 2026-07-01 regression fix removed the previous 120-BPM
  duration-inference dependency, and hardware LED smoke passed on both decks.
  Beat Jump normal pad LED output is implemented from P4-owned loaded-track
  state while Beat Jump mode is selected. Shifted helper LED 7/8 output is
  implemented from the same loaded-track/mode state gated by held deck Shift;
  broader shifted mirror LEDs remain deferred. Post-flash FLX4 smoke passed on
  2026-07-07: all 8 normal Beat Jump pads lit in Beat Jump mode with a loaded
  track, and shifted helper LED 7/8 lit while deck Shift was held.
  The 2026-07-07 LED batch smoke passed for Censor, Cue+Shift / track-start,
  Loop Adjust In, Loop Adjust Out, Track Load Deck 1/2, and post-removal pad
  mode LED behavior.
  Cue+Shift / track-start and Loop Adjust In/Out now emit P4-owned momentary
  LED flashes, and Track Load Deck 1/2 follows the P4 audio-engine loaded
  state with reconnect refresh. Post-flash FLX4 behavior smoke for these
  outputs passed on 2026-07-07.
  Beat FX ON/OFF LED output is implemented from P4 Beat FX enabled state and
  hardware smoke passed on 2026-07-01. Pad FX normal pad LED hardware smoke
  passed on 2026-07-01. Master Cue LED output is implemented from P4 monitor
  state and is included in reconnect snapshots; hardware smoke passed on
  2026-07-02. Pad-mode, Beat Sync, and active Loop In/Out LED
  hardware smoke has passed where recorded in
  `docs/validation/FLX4_LED_MIDI_OUT_CAPTURE.md`; full manual USB replug
  LED-state acceptance passed on 2026-06-26. S3 reset recovery after the extended reconnect snapshot no
  longer crashes, and P4-only reset recovery is implemented through S3 heartbeat
  connected-state refresh with hardware smoke passed on 2026-06-26.
- [x] Final hardware-smoke testing of the integrated Phase 7 input surface and record any exceptions from the XML mapping.

See Phase 7 in `docs/DEVELOPMENT_PLAN.md`. XML status/midino values are now the
implementation seed because the physical MVP capture matched them exactly;
Mixxx JavaScript behavior is not imported.

## S3 Debug AP Smoke Test

Runtime S3 Wi-Fi debug AP on `master`. Hardware smoke passed on 2026-07-08
(S3 on COM6, P4 on COM15) after two fixes below:

- [x] Flash current S3 (COM6) and P4 (COM15) firmware.
- [x] Boot with the P4 Settings `S3 DEBUG AP` switch OFF; no `Pajoniiir-S3-DEBUG`
  AP visible.
- [x] Enable the switch; S3 brings up SoftAP + DHCP on `192.168.4.1`
  (`s3_debug_ap: S3 debug AP active` on the S3 console).
- [x] Connect a phone to `Pajoniiir-S3-DEBUG`, open `http://192.168.4.1`; page
  loads and **live logs stream** over SSE without disconnecting.
- [ ] FLX4 MIDI / P4-to-S3 headphone audio responsive while ON — not retested
  this session (FLX4 was unplugged; re-verify when the controller is attached).
- [ ] Reboot P4 with the AP left ON; confirm P4 sends OFF at boot (boot-time OFF
  frame is wired but not separately observed this session).

Two issues were found and fixed during the smoke:

1. **Wi-Fi owner conflict.** `wifi_debug_log` (build-time UDP log, STA mode) and
   `s3_debug_ap` (runtime AP mode) both drive the S3 radio and are mutually
   exclusive. A stale local `sdkconfig` had `CONFIG_WIFI_DEBUG_LOG_ENABLED=y`
   with real credentials, so the S3 came up in STA and `start_ap()` never
   produced the AP. Kconfig default is `n`; keep `wifi_debug_log` disabled when
   using the runtime debug AP (the AP replaces it).
2. **httpd `/events` crash.** The SSE handler overflowed the 4 KB default httpd
   task stack (2 KB local buffer) and reset the S3 as soon as a browser opened
   the stream. Fixed by raising `config.stack_size` to 8 KB, shrinking the
   per-tick buffer, and streaming only new lines (`last_seq`) instead of the
   whole ring each second.

## FLX4 USB Audio Product Smoke

Current product topology: PCM5102A RCA MAIN from the P4, FLX4 USB headphones as
CUE/MONITOR through the P4-to-S3 I2S monitor link and S3 USB Audio streamer.

- [x] Build S3 with `sdkconfig.defaults;sdkconfig.flx4_hp_e2e`.
- [x] Flash S3 product build to `COM6` after the 2026-07-09 rate-match fix.
- [x] Confirm ring stream starts with the P4 link rate and that
  `P4_AUDIO_LINK rx blocks` rises steadily.
- [x] Confirm `gaps=0` and `crc=0` during steady playback.
- [x] Confirm `overruns=0` during steady playback; ring fill should oscillate
  below the 4096-frame ceiling instead of staying pinned at full.
- [x] Confirm `FLX4_USB_AUDIO submitted` and `completed` rise together with
  `skipped=0` and `underrun=0`.
- [x] Confirm audio remains audible in FLX4 headphones and PCM5102A MAIN output
  remains active.

## Controller Profile Setup And Verification

Data-driven multi-controller platform (Phase 11). To run the DDJ-FLX4 (or any
supported controller) through a profile instead of the built-in map:

1. Compile the profile if needed:
   `python tools/controller_profile/compile_profile.py controllers/pioneer_ddj_flx4/profile.json -o controllers/pioneer_ddj_flx4/profile.s3bin`
   (the committed `.s3bin` is already up to date; the S3 host runner fails if it
   drifts from `profile.json`).
2. Copy to the SD/TF card so the layout is `SD:/controllers/<name>/profile.s3bin`
   (directory per controller; `profile.json` may sit alongside and is ignored by
   the firmware), or upload the compiled file from the P4 Wi-Fi Remote
   **CONTROLLER PROFILE** card. Rekordbox media stays on the USB drive.
3. Insert the SD into the P4 and boot.

Verification (hardware, 2026-07-09 — profile-loading path confirmed):

- [x] SD card populated: `controllers/pioneer_ddj_flx4/profile.s3bin`
  byte-identical to the repo, S3CP header + CRC valid.
- [x] P4 boots with the card and mounts `/sd`.
- [x] P4 scans `/sd/controllers` at boot and loads the profile into the
  registry — confirmed via `http://192.168.4.1/api/status` reporting
  `"controller":{...,"profiles":1}` over the `Pajoniiir` Wi-Fi remote AP.
- [ ] Connect the DDJ-FLX4 to the S3: S3 sends the descriptor, P4 matches and
  streams the profile (`profile 'pioneer_ddj_flx4' transfer to S3 OK`),
  `/api/status` shows `"present":true`,
  `"profile_state":"active"`, and
  `"active_profile":"pioneer_ddj_flx4"` only after the S3 ACKs
  `PROFILE_ACTIVATE`; then confirm FLX4 controls/LEDs work through the dynamic
  profile — pending controller being attached to the S3.

Notes:

- Web upload requires a profile ID containing only letters, digits, `_` and
  `-` (1-39 characters), a 32-16384 byte `.s3bin`, and an explicit overwrite
  confirmation if the ID already exists. See `CONTROLLER_PROFILE_UPDATE.md`.
- [ ] Hardware-accept profile overwrite, automatic S3 reactivation, reboot
  persistence, corrupt/truncated rejection and interrupted-upload recovery.
- [ ] Unplug the FLX4 from the running S3 and confirm `/api/status.controller`
  changes to `"present":false`, the active profile clears, and
  `/api/diagnostic-log` gains exactly one `CONTROLLER_DISCONNECTED` record;
  reconnect and confirm descriptor matching/profile activation recover.
- [ ] During a controlled control-link fault injection, confirm
  `/api/status.control_link.crc_errors` and/or `sequence_gaps` increase and the
  service journal receives the corresponding `CONTROL_LINK_CRC_ERROR` /
  `CONTROL_LINK_GAP` summary. In a normal steady run both counters should stay
  at zero.
- [ ] Confirm `/api/status.service_log` reports the expected SD availability,
  bounded queue depth/capacity, drop/write counters, current file bytes and
  last writer error while the service journal is active.

- P4 serial is native USB-Serial-JTAG (COM15); early-boot app logs (including the
  `ctrl_profile:` scan line) are lost to non-interactive pyserial capture before
  the host CDC connection stabilises. Use `idf.py -p COM15 monitor` in a real
  terminal for a lossless boot log, or `/api/status` for the profile state.
- A stale `/sd/controllers` (SD missing or no profiles) is a boot warning, never
  a boot blocker.

## One-time OTA partition migration

Before installing the boards in an enclosure, perform one full wired flash of
both targets using the OTA-enabled build. The flash must include bootloader,
partition table, initial OTA data, and application; flashing only the app binary
does not migrate the legacy single-app layout.

- P4 flash arguments must include `ota_data_initial.bin` at `0x10000` and the
  factory application at `0x20000`.
- S3 flash arguments must include `ota_data_initial.bin` at `0x10000` and the
  initial `ota_0` application at `0x20000`.
- Confirm each boot logs `fw_health` with the expected slot and image state.
- Keep USB access available until a successful OTA and rollback cycle has been
  completed on both targets.

HTTP OTA implementation status:

- P4 Wi-Fi Remote exposes `/api/firmware` and `/api/ota/p4` plus its firmware
  upload panel. Code/build/bootstrap flash and `factory -> ota_0 -> ota_1`
  hardware acceptance are complete.
- S3 Debug AP exposes `/api/firmware`, `/api/ota/s3`, and `/update`. Code, build,
  wired bootstrap flash, and `ota_0 -> ota_1 -> ota_0` hardware acceptance are
  complete.
- Both targets reject missing/wrong target headers, undersized payloads, and the
  other target's chip image with HTTP 400 before activation.
- A client disconnect after 64 KiB aborts the inactive-slot write without a
  reboot on both targets; a following complete upload succeeds normally.
- Each new slot booted as `pending_verify` and was marked valid only after the
  mandatory startup health check. Test-only non-confirming P4 and S3 images both
  rolled back to the prior valid slot.
- A valid target header declaring one byte more than the inactive slot capacity
  is rejected with `ESP_ERR_INVALID_SIZE` on both targets.
- Physical power loss during an inactive-slot write leaves the current image
  bootable on both targets. The accepted S3 run used a throttled 20 KiB/s upload
  to guarantee the power cut occurred before transfer completion.
- AP testing passed on 2026-07-13 with Internet retained over `Ethernet 2` while
  Wi-Fi was dedicated to `Pajoniiir` and `Pajoniiir-S3-DEBUG` in turn. After the
  destructive tests, both targets were OTA-restored to packaged release
  `RC1-106-g717b6ab3`; final status was P4 `ota_0 / valid` and S3
  `ota_0 / valid`.

Batch 5 firmware-status verification:

- S3 sends `0xA6 FIRMWARE_REPORT` with slot, image state, and version alongside
  each five-second heartbeat.
- P4 logs the first/changed report as `S3 firmware version=... slot=... state=...`.
- P4 Settings must show both `P4: version [slot]` and
  `S3: version [slot/state]` rather than a hard-coded firmware label.
- `tools/package_ota_release.ps1` creates an ignored dual-target package and
  fails if versions, chip IDs, project metadata, or slot limits disagree. It
  now produces signed `.ddjota` bundles plus a signed outer manifest.

Batch 6 signed-OTA transition and acceptance:

- [x] back up `keys/ota_signing_private.pem` in restricted offline storage
  (offline USB confirmed 2026-07-14);
- [x] install the signed-OTA-capable P4 and S3 firmware once by full wired flash
  (preferred) or through each still-running legacy unsigned endpoint;
- [x] upload valid signed P4 and S3 bundles and confirm version, opposite slot,
  and successful mandatory startup/validation;
- [x] confirm audio, UI, controller and LED behavior after the signed updates;
- [x] confirm modified manifest, modified image and wrong target are rejected on
  both targets without activating the inactive slot;
- [x] confirm wrong key ID/key, chip/project mismatch and truncated/extended
  bundles are rejected on hardware;
- [x] repeat interrupted-upload and forced-rollback checks with signed bundles;
- [x] record the accepted release version, key ID and final slots/states before
  enclosing the boards.

Signed E1 acceptance record, 2026-07-14: release `RC1-123-g587cd7a1`, key ID
`rel-001`; final P4 `ota_0`, S3 `ota_1`, both valid/operational. Both targets
rejected wrong key/key ID, chip/project mismatch and truncated/extended bundles;
both survived a 128 KiB interrupted upload and rolled back from signed
`ROLLBACK-TEST-*` images. Final UI/touch, dual playback/scratch, controller,
LED, MAIN and headphone-cue smoke passed.

Signed deployment record, 2026-07-16: matching `RC1-131-gc391e306` bundles
with key ID `rel-001` were uploaded successfully to both targets. P4 moved
`ota_0 / RC1-126-g812ad70f -> ota_1 / RC1-131-gc391e306`; S3 moved
`ota_1 / RC1-123-g587cd7a1 -> ota_0 / valid / RC1-131-gc391e306`. P4 status
was healthy after reboot and its nested report confirmed S3. This was an OTA
deployment/boot check, not a full functional smoke; the latest fully accepted
functional baseline remains E1 `RC1-123-g587cd7a1`. See
[`validation/SIGNED_OTA_RC1_131_DEPLOYMENT.md`](validation/SIGNED_OTA_RC1_131_DEPLOYMENT.md).

Bench display record, 2026-07-17: development build
`RC1-132-g2b0cfd59-dirty` first passed the focused 132-second smoke. The fix was
then committed at `bd5e43ce`, built and packaged for both targets as signed
candidate `RC1-133-gbd5e43ce`, and independently verified with key `rel-001`.
The exact P4 payload was wired-flashed to factory over COM15 and passed the
more-than-71-second re-smoke above. S3 was built only for the canonical paired
package and was not flashed. This wired deployment did not exercise a new
P4 OTA-slot transition and is not the full RC1 functional checklist.

Wired report smoke passed on 2026-07-13: after a P4-only restart COM15 logged
`S3 firmware version=RC1-104-g2f710fb7-dirty slot=1 state=3` at 3150 ms.

R5 cleanup status:

- [x] R5A call-graph, signed-size and legacy S3 build baseline recorded;
- [x] R5B single-deck P4 audio facade removed and callers migrated to the
  authoritative deck API;
- [x] R5C mixer entry points consolidated;
- [x] R5D legacy S3 mode explicitly retired; components and build branch removed;
- [x] R5E independent scratch allocation/copy path removed; canonical timeline
  failure keeps ring playback and selects platter-hold on touch;
- [x] R5F final call-graph audit, host suites, signed builds and size comparison
  recorded;
- [x] R5F clean P4 image wired-flashed and monitored without reset/panic/watchdog;
- [x] R5F clean S3 image wired-flashed and final dual-target scratch soak accepted.
