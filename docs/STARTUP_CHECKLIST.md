# P4 operation and release checklist

Status: **current P4 checklist, reconciled 2026-09-29**.

## Normal startup

- [ ] Use the accepted regulated 5 V supply and unchanged protected USB0/USB1
  VBUS wiring.
- [ ] Connect PCM5102A MAIN RCA and, if needed, FLX4 headphones before raising
  amplifier levels.
- [ ] Insert Rekordbox media in USB0 and connect DDJ-FLX4 to USB1.
- [ ] Power on and wait for the display, Library and FLX4 profile/MIDI/UAC to
  become ready.
- [ ] Load one track to each deck and confirm PLAY, waveforms, MAIN and cue.
- [ ] Treat a reboot, brownout, TWDT, missing root or latched UAC loss as a
  failure; do not continue a performance test from an unexplained reset.

## Media and controller

- [ ] [Package H preview](validation/FORK_IMPROVEMENTS_PACKAGE_H_PROGRESS_20261004.md):
  default-off, Overview, Library and Hot Cues in the actual firmware. Do not treat
  prototype Settings captures as completed runtime parity. Physical touch,
  render, MAIN/cue and strict timing gates remain NOT RUN. Network OTA installation
  is authorized when reachable. H3 failed Wi-Fi/USB acceptance; verified previous
  firmware was restored on explicitly authorized COM15 with matching flash
  read-back. AP/API and USB0 mount recovered; confirm FLX4 reconnection separately.
  The one-time wired recovery is complete; ignore unrelated devices. Verify project/image and idle
  decks before an install, then collect exact-image/operator acceptance separately.

- [ ] [Package G](validation/FORK_IMPROVEMENTS_PACKAGE_G_SOFTWARE_20261004.md):
  all card/recording/audio gates NOT RUN. Record board, card model/capacity and
  exact image; compare idle-on/off and JC1060 internal/PSRAM+bounce builds.
  Test full card, removal, power loss, STOP timeout and `.part` recovery;
  capture SD/gate/fsync, heap/stack and strict audio/USB counters with listening.
  Keep recorder and SD workaround off in ordinary releases. Ignore COM devices
  from other projects; future installation uses authorized OTA only.

- [ ] [Package E](validation/FORK_IMPROVEMENTS_PACKAGE_E_SOFTWARE_20261004.md):
  JC1060 bring-up is NOT RUN. Verify panel/revision, flash/PSRAM, USB roles,
  touch/render, SD and Ethernet. Do not enable PCM5102A: pins overlap Ethernet.
  ES8311 is disabled in the USB-only configuration.

- [ ] [Package F](validation/FORK_IMPROVEMENTS_PACKAGE_F_SOFTWARE_20261004.md):
  all physical gates NOT RUN. Capture DDJ-400 descriptors; verify MAIN channels
  1/2 and cue 3/4 independently, full-scale packing, reconnect and UAC restart.
  Repeat FLX4/PCM5102A listening, strict counters and 180-minute dual-deck soak.
  Verify stopped-only sink changes and rejection during LOAD/PLAY/scratch/recording.

- [ ] [Package D](validation/FORK_IMPROVEMENTS_PACKAGE_D_SOFTWARE_20261004.md):
  confirm DDJ-400 initial SysEx, MIDI/LED behavior, CH1/CH2/MASTER selector,
  filter travel and reconnect on the exact candidate. NOT RUN. MASTER uses
  both-deck FX; physical UAC acceptance remains NOT RUN. V4 requires development firmware;
  M2.4 rejects it. Recompile JSON for rollback; never patch version bytes.

- [ ] For the [fork improvements branch](FORK_IMPROVEMENTS.md), verify titles
  against the real Rekordbox export and confirm beat number 1 is accented,
  including a grid starting on beat 3. Host fixtures are not physical acceptance.
- [ ] Before hardware acceptance of [software steps A and B1–B5](validation/FORK_IMPROVEMENTS_HANDOFF_20261004.md),
  compare MP3/WAV/FLAC tail seeks, paused CUE/scratch and active loop resize
  against the existing device, then run MAIN/cue listening and the exact-image
  dual-deck soak. These checks remain **NOT RUN**.

- [ ] Use FAT32 or exFAT media on superfloppy, MBR or GPT layouts.
- [ ] Confirm the expected Rekordbox library count before relying on the media.
- [ ] For a reconnect test, record which root was removed and avoid accidental
  movement of the other cable.
- [ ] For package C on the fork-improvements branch, compare playlist order and
  nested navigation with the real export. Browse quickly during dual-deck
  playback; verify artwork matches each track after page changes and USB
  removal, PWV4 falls back on missing/corrupt EXT, and MAIN/cue remain clean.
  These physical checks are **NOT RUN**.
- [ ] On the fork improvements branch, compare Rekordbox hot cues with pad LEDs
  and the Hot Cues screen. Delete a source cue, reload the track and confirm it
  stays deleted; verify local cues do not appear on a second export sharing a
  raw track ID. Physical confirmation remains **NOT RUN**.
- [ ] Hold `HOLD RESTORE` on Hot Cues and confirm source cues return for the
  selected deck, with refreshed labels and LEDs. A short tap must preserve
  local edits. Physical confirmation remains **NOT RUN**.
- [ ] Compare a touch hot-cue press with the matching FLX4 pad: an empty slot
  stores, a point recalls and exits the loop, a loop cue recalls its bounds,
  and both inputs preserve play/pause. Physical confirmation: **NOT RUN**.
- [ ] Check CUE set while paused, held preview, release-to-cue and PLAY while
  CUE is held, using both touch and FLX4. Disconnect during preview must pause
  and return to cue. Physical/listening confirmation: **NOT RUN**.
- [ ] Verify the B13b touch VINYL/CDJ selector and B18 `jog_vinyl`/`jog_cdj`
  profile bindings on matching development firmware. Verify
  switching during platter touch, playing bend and paused scrub independently
  on both decks. B13a-B13b verify software only; physical acceptance:
  **NOT RUN**.
- [ ] B14: compare a shorter decoded file and a longer file with truncated
  analysis. Check live elapsed/remaining time, touch seek, fixed zoom waveform
  timing and blank mini-waveform tail. Hardware/listening acceptance: **NOT RUN**.
- [ ] B15: repeat beat-jump and shift+jog through the analysis tail and near
  actual EOF on both decks, then rapid LOAD/unload/reload. Check pause/play
  continuity and deck isolation. Physical acceptance: **NOT RUN**.
- [ ] B16: compare cold ANLZ parsing and warm metadata-cache loads for a real
  export with whole-second PDB rounding. Check zoom/preview alignment and tail
  playback; missing or capped PWV3 must retain the fallback. Physical acceptance:
  **NOT RUN**.
- [ ] B17: play CBR/VBR MP3 without Xing/VBRI from the beginning through the
  final audio tail, with both short and long analysis spans. Check EOF drain,
  remaining time, rewind and seek/loop cancellation of duration measurement.
  Read faults must not publish a successful measured EOF. Physical/listening
  acceptance: **NOT RUN**.
- [ ] B19: measure complete-index load/seek latency while the other deck plays;
  compare first/last samples, paused cue scratch and loop wraps with actual
  exports. Exercise cancellation, SD/USB removal, fallback files and the
  `SEEK INDEX ERR` path. Record internal heap, PSRAM, worker stack headroom and
  strict audio/USB counters on the exact candidate SHA. **NOT RUN**.
- [ ] [Package B software closure](validation/FORK_IMPROVEMENTS_PACKAGE_B_SOFTWARE_20261004.md)
  does not qualify hardware. Repeat MAIN/cue listening and the final 180-minute
  dual-deck soak before release; signed OTA/recovery remain **NOT RUN**.
- [ ] On the fork improvements branch, LOAD LOCK starts enabled. Confirm a
  playing destination deck rejects touch, FLX4 and Web Remote LOAD without
  changing its track; pause/stop that deck before loading another track.
  Physical confirmation remains **NOT RUN**.
- [ ] After FLX4 reconnect, confirm profile, MIDI IN/OUT, UAC, LEDs and held
  controls have converged to the P4-owned state.
- [ ] Do not advertise a non-FLX4 profile from host fixtures alone.
- [ ] Before installing scaled CC profiles, confirm firmware supports S3CP v3.
  M2.4 accepts only v2; keep the installed v2 profiles until firmware update.
  See [M2.5 scaling validation](validation/P4_M2_5_CC7_SCALING_20261002.md).

## Wi-Fi Remote

- [ ] Enable **Wi-Fi Remote** in Settings only when the service surface is
  needed.
- [ ] Connect to `Pajoniiir`, then open `http://pajoniiir.local` or
  `http://192.168.4.1`.
- [ ] Expect the local AP to disappear temporarily during a TEST CONNECTION,
  CHECK FOR UPDATE or pull-update STA visit.
- [ ] Reconnect to the restored AP and verify authoritative device state before
  issuing more commands.

## Signed OTA

- [ ] Stop playback and keep power stable.
- [ ] Record version, slot and current health before the update.
- [ ] Upload only `main-deck-p4.ddjota`; raw `.bin` is wired-recovery material.
- [ ] After reboot, verify the expected version, opposite slot, empty OTA
  error, valid boot state, USB0 Library, FLX4 control/LED/UAC, MAIN and cue.
- [ ] Use local signed push OTA for intentional rollback; public pull OTA is
  newer-only.
- [ ] Never expose or commit the private signing key or hosting credentials.

## Before a firmware commit

- [ ] Initialize ESP-IDF v6.0.2 and confirm `idf.py --version`.
- [ ] Run `./tests/run_p4_host_tests.ps1` for shared/firmware behavior.
- [ ] Run the UI simulator gate when UI rendering or navigation changes.
- [ ] Build `firmware/main-deck-p4` with ESP-IDF v6.0.2.
- [ ] Confirm `firmware/main-deck-p4/dependencies.lock` changed only when the
  dependency resolution is intentionally updated.
- [ ] Run `git diff --check` and review the exact staged file set.

## Before a release

- [ ] For M2.5, prove automatic USB0/USB1 recovery after software restart and
  signed opposite-slot OTA with reboot diagnostics disabled; confirm FLX4 and
  each newly advertised controller on real hardware, including MAIN/cue.
- [ ] Freeze a clean commit and run complete CI/build/package gates.
- [ ] Create a new immutable version; never move `M2`, `M2.1`, `M2.2`, `M2.3` or `M2.4`.
- [ ] Build and sign from the exact tag, then independently verify bundle and
  manifest signatures/hashes.
- [ ] Install the exact tagged image and run impact-appropriate physical,
  audible and duration gates.
- [ ] Publish the versioned OTA bundle before updating `latest.json`.
- [ ] Verify public URL, size, SHA-256 and GitHub Release assets independently.
- [ ] Record accepted limitations without converting waived or unrun checks
  into passes.

## Requalification triggers

Repeat the relevant electrical/lifecycle/audio/OTA gates after changes to:

- power supply, VBUS isolation/protection, enclosure or cable routing;
- USB host ownership, FIFO, recovery or controller profile lifecycle;
- decoder/cache/filesystem, audio scheduling, Master Tempo, effects or sinks;
- Wi-Fi transition, web mutation, signing, OTA partitions or trust keys;
- display/touch timing, UI ownership or controller-to-UI dispatch.

The M2.4 release evidence is indexed in [`README.md`](README.md). Current
accepted limitations are in [`DOCUMENTATION_STATUS.md`](DOCUMENTATION_STATUS.md)
and [`RISK_REGISTER.md`](RISK_REGISTER.md).
