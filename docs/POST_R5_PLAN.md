# Post-R5 Plan

Status: planned 2026-07-14. R5A-R5F remediation is complete and hardware
accepted at `RC1-121-gb7ac66a5`. This document is the ordered continuation plan
for enclosure readiness and production hardening.

## Execution Order

The batches below are intentionally ordered so destructive OTA and wired
recovery tests finish while both USB service ports are still accessible.

## E1 — Signed OTA Hardware Acceptance

Goal: close the remaining signed-update and rollback gates on both targets.

Tasks:

1. Back up `keys/ota_signing_private.pem` in restricted offline storage.
2. Package one matching P4/S3 signed release with the current `rel-001` key.
3. OTA-update P4 and S3 once and record version, source/destination slot and
   final image state.
4. After each update, smoke audio, UI, FLX4 input, LED output and headphone
   cue monitoring.
5. On hardware, reject wrong key ID/key, chip/project mismatch and
   truncated/extended bundles without activating the inactive slot.
6. Repeat interrupted upload and forced rollback on both targets.
7. Restore both targets to the accepted matching release and record final
   slot/version/state.

Acceptance:

- valid signed bundles update the intended target and become `valid`;
- every negative case leaves the current valid image bootable;
- forced rollback returns to the previous valid slot;
- P4 and S3 finish on the same recorded release;
- wired recovery remains available throughout the batch.

## E2 — Enclosure Wiring And Service Readiness

Goal: prove the final wiring is electrically safe and serviceable before the
boards become difficult to reach.

Tasks:

1. Verify common ground across P4, S3, controller and audio hardware.
2. Verify that independent 5 V sources cannot back-feed one another.
3. Check UART and P4-to-S3 PCM-link wiring against `HARDWARE_WIRING.md`.
4. Validate the final MAIN, cue/headphone and USB host cable routing.
5. Preserve an accessible wired-recovery connector or validate an equivalent
   service harness for each processor.
6. Run both host suites and both signed firmware builds from a clean checkout.

Acceptance:

- wiring matches the documented pinout and power topology;
- no back-feed or unstable shared-ground condition is present;
- both recovery paths can connect without reopening the finished enclosure;
- clean-checkout host/build gates pass.

## E3 — Closed-Enclosure Endurance Soak

Goal: validate audio timing, thermal margin and RF reachability under sustained
worst-case product load.

Tasks:

1. Run both decks simultaneously with representative MP3/WAV/FLAC material.
2. Exercise Vinyl scratch, fast forward/reverse motion, release/re-grab,
   platter hold and near-EOF restart behavior on both decks.
3. Run Master Tempo off/on and simultaneously on both decks, including material
   with large tempo offsets.
4. Keep serial diagnostics for at least 60 seconds per focused scenario and run
   a multi-hour unattended audio soak.
5. Monitor reset reason, watchdog, stack, heap/PSRAM, PCM ring/timeline,
   limiter, P4-to-S3 gaps/CRC and USB headphone underrun/overrun counters.
6. Measure enclosure temperature near both processors, regulators and audio
   hardware.
7. Verify P4 Wi-Fi Remote and S3 Debug AP reachability with the enclosure closed.

Acceptance:

- no panic, watchdog, brownout, unexpected reset or latched platter;
- no persistent PCM/link/USB audio underrun or overrun;
- acceptable key-lock quality and CPU margin on both decks;
- stable temperature and usable AP range in the final enclosure.

## E4 — Remaining Targeted Hardware Smokes

Goal: close the remaining integration rows that are independent of the main R5
cleanup.

Tasks:

1. Verify S3 input snapshot replay after restarting only P4 while S3/FLX4 stay
   powered and mixer controls remain untouched.
2. Run S3 Debug AP while FLX4 MIDI and P4-to-S3 headphone audio are active.
3. Restart P4 with S3 Debug AP enabled and confirm the boot-time OFF command
   returns the AP to the required default-off state.
4. Confirm the SD-card FLX4 profile reaches `active` only after S3 activation
   ACK, then smoke controls, LEDs, reconnect and fallback.
5. Expand LED feedback only for explicitly selected remaining P4-owned states;
   shifted/mirror LEDs remain deferred until assigned to a concrete batch.

Acceptance:

- P4 reconstructs continuous mixer state after its isolated restart;
- Debug AP does not disrupt MIDI or headphone audio;
- boot-time AP state is deterministic;
- dynamic FLX4 profile activation and fallback are observable and correct.

## E5 — Production Hardening And Platform Expansion

Goal: address work that is valuable after enclosure acceptance but is not a
prerequisite for the next hardware assembly step.

Tasks:

1. Define production signing-key custody, provisioning and rotation beyond the
   current `rel-001` development key.
2. Move controller-triggered UI/library work from the `deck` task to the
   LVGL/UI task context.
3. Validate one non-FLX4 controller profile end to end before advertising
   generic controller compatibility.
4. Continue long two-deck Master Tempo quality/CPU regression testing.
5. Leave the one-time initial pad sweep as a low-priority cosmetic issue unless
   a specific functional MIDI/state fault becomes reproducible.

Acceptance:

- signing and recovery processes are documented for production operation;
- controller input cannot invoke heavy LVGL/library work on the control task;
- the profile platform is demonstrated with at least one non-FLX4 device;
- remaining cosmetic work does not displace functional or safety gates.

## Resume Point

Start the next session with **E1 — Signed OTA Hardware Acceptance**, task 1:
confirm the private-key backup location and inspect the current release-package
inputs before generating or uploading a new bundle.

Keep `STARTUP_CHECKLIST.md`, `DEVELOPMENT_PLAN.md`, `DOCUMENTATION_STATUS.md`
and this plan synchronized as each acceptance gate closes.
