# P4 operation and release checklist

Status: **current M2.1 checklist, reconciled 2026-09-20**.

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

- [ ] Use FAT32 or exFAT media on superfloppy, MBR or GPT layouts.
- [ ] Confirm the expected Rekordbox library count before relying on the media.
- [ ] For a reconnect test, record which root was removed and avoid accidental
  movement of the other cable.
- [ ] After FLX4 reconnect, confirm profile, MIDI IN/OUT, UAC, LEDs and held
  controls have converged to the P4-owned state.
- [ ] Do not advertise a non-FLX4 profile from host fixtures alone.

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

- [ ] Freeze a clean commit and run complete CI/build/package gates.
- [ ] Create a new immutable version; never move `M2` or `M2.1`.
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

The M2.1 release evidence is indexed in [`README.md`](README.md). Current
accepted limitations are in [`DOCUMENTATION_STATUS.md`](DOCUMENTATION_STATUS.md)
and [`RISK_REGISTER.md`](RISK_REGISTER.md).
