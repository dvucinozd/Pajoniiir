# P4 Startup and Release Checklist

Status: **active P4-only checklist, reconciled 2026-09-13**.

## Repository and build

- [x] Branch is `feat/p4-dual-usb-host`.
- [x] Installed validation firmware commit `495947e` is pushed.
- [x] ESP-IDF v6.0.2 is the only supported SDK.
- [x] Complete P4 host suite passes.
- [x] Clean `build_signed` and signed-bundle verification pass.
- [x] `dependencies.lock` is tracked and unchanged by the exact build.
- [x] Install and focused-smoke `RC2-128-g495947e` on hardware.
- [ ] Repeat all automated gates from a fresh checkout for the final candidate.

## Latest installed exact-image evidence

- [x] `RC2-128-g495947e` installed on `ota_1` with empty OTA error.
- [x] USB0 mounts and exposes the 100-track Library.
- [x] Direct FLX4 profile, MIDI IN and USB audio activate on USB1.
- [x] First web PLAY after more than 120 seconds idle executes immediately.
- [x] Idle lifetime UAC underflow does not set active `data_loss`.
- [x] Thirty-second dual-deck window has zero drop/overflow/underflow/late
  deltas.
- [x] Thirty-minute exact-image dual-active MP3 seek/restart soak passes.
- [x] Targeted three-hour dual-MP3 limiter/WDT soak holds one boot epoch with
  zero PCM underrun or active UAC loss and no observable USB/controller/output
  failure.
- [x] Fourteen rare output-late warnings analyzed; maximum `12,169 us` versus
  `11,610 us` warning threshold, with no downstream failure and no justified
  code change.

## Electrical qualification

- [x] Verify common ground.
- [x] Verify independent sources cannot backfeed each other.
- [x] Isolate native VBUS and provide protected, current-limited USB0 and USB1
  device-side VBUS.
- [x] Measure 5 V at idle, cold start, enumeration, track load and sustained
  dual-deck playback.
- [x] Confirm maximum and sustained current and brownout margin are inside the
  defined limits; raw numeric readings were not preserved.
- [ ] Repeat measurements in final enclosure wiring.

## Dual-USB lifecycle matrix

- [x] Cold boot with both devices attached: Group A 4/4.
- [x] Warm/software reboot with both devices attached: Group B 4/4.
- [x] Boot empty, then attach USB0 followed by USB1: Group C 4/4.
- [x] Boot empty, then attach USB1 followed by USB0: Group D 4/4.
- [x] USB0 idle remove/reinsert while FLX4 remains active: Group E 5/5.
- [x] Implement and host-test a guarded deterministic Group F Library-load
  removal trigger; ESP-IDF 6.0.2 compile-validation passes.
- [x] Commit, sign, install and smoke the exact Group F firmware image.
- [x] USB0 remove during Library load: Group F 5/5.
- [x] Implement and host-test a guarded deterministic Group G audio-load
  trigger after the first 32 KiB compressed-cache read; ESP-IDF 6.0.2 build
  passes.
- [x] Commit, sign, install and smoke the exact Group G firmware image.
- [x] USB0 remove during audio load: Group G 5/5 with alternating D1/D2
  deterministic first-cache-page triggers.
- [x] Implement and self-test the Group H USB1 idle reconnect harness while
  preserving the USB0 100-track Library.
- [x] USB1 idle disconnect/reconnect while USB0 remains mounted: Group H 5/5.
- [ ] USB1 disconnect/reconnect during dual-deck playback.
- [ ] Confirm MIDI, LEDs and UAC recover without duplicate recovery epochs.
- [ ] Confirm held controls release and no scratch/pad/shift state remains
  latched.
- [ ] Complete 50 controlled lifecycle cycles, including at least 20
  independent physical reconnects. Current progress: 36/50 cycles and 36
  planned attachment/reconnect actions; Groups I--L remain.
- [ ] Confirm software reboot and OTA recover both roots without manual reinsert.

## Media and audio

- [ ] Re-export WAV and FLAC fixtures and verify physical `Contents` files.
- [ ] Run mixed MP3/WAV/FLAC dual-deck load, seek, loop, CUE, scratch and EOF.
- [ ] Confirm BNA recovery and locked-backend-read counters remain acceptable.
- [ ] Measure worst-case dual Master Tempo CPU/I2S deadlines on P4.
- [ ] Listen for clicks, flat-top clipping, pitch artefacts and MAIN/cue defects.
- [ ] Exercise both decks with Master Tempo off/on and near-EOF scratch/hold.
- [ ] Complete detailed Beat FX CH1/CH2/1&2 transition and tail checks.

## Wi-Fi, web, profile and OTA

- [ ] Re-smoke hardened pull OTA AP-to-STA-to-AP transition.
- [ ] Verify newer-only, offer TTL, size, SHA-256 and signature rejection paths.
- [ ] Verify interrupted/slow upload recovery and subsequent server availability.
- [ ] Verify signed push OTA rollback and opposite-slot boot.
- [ ] Verify guarded control, load, seek and profile mutations.
- [ ] Verify USB0 and FLX4 recover automatically after OTA reboot.

## Product soak and enclosure

- [ ] Define and run the final multi-hour combined-load soak.
- [ ] Include seeks, restart, Master Tempo, scratch, loops, Hot Cues, FX, MIDI,
  LEDs, MAIN/cue, USB cache and web status traffic.
- [ ] Require no reset, brownout, lost mount/controller, latched control or
  gated error-counter increase.
- [ ] Measure closed-enclosure power and temperature margins.
- [ ] Verify RF/AP reachability and connector strain relief.
- [ ] Preserve an accessible wired recovery/service path.

## Production and release

- [ ] Decide per-device service credentials versus documented accepted risk.
- [ ] Define production signing-key custody and rotation.
- [ ] Decide Secure Boot, Flash Encryption, PMF/WPA3 and irreversible
  provisioning policy.
- [ ] Select and pin an SPDX/CycloneDX SBOM generator if required for release.
- [ ] Freeze the final commit and rerun automated gates.
- [ ] Build, sign, verify and install the exact final image.
- [ ] Run the complete manual product smoke.
- [ ] Publish hashes, slot/version evidence and all remaining acceptance results.
- [ ] Merge only after every mandatory gate is closed, then create the release
  tag.

Detailed procedure: [`migration/P4_DUAL_USB_NEXT_SESSION.md`](migration/P4_DUAL_USB_NEXT_SESSION.md).
