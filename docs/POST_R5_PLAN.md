# Post-R5 Plan

Status: active and reconciled 2026-07-26. R5A-R5F remediation and E1 signed-OTA
acceptance are complete. The latest clean dual-target release build is
`RC1-259-gdaf4639`; the last known bench state has both boards matched at
`RC1-254-g21f21963` after the P4 pull-OTA path was proven end to end on
2026-07-24. The clean candidate has not yet been signed, packaged or deployed.
The latest **fully** functionally accepted release still remains
`RC1-123-g587cd7a1` (accepted on P4 `ota_0` and S3 `ota_1` on 2026-07-14).

The master-output recorder is shelved and compiled out by default through
`CONFIG_AUDIO_RECORDER_ENABLED=n`. Long soaks on the original microSD card
showed card-level `fwrite` stalls that drained the 2.95 s ring. A replacement
card produced encouraging first-five-minute figures but the required 25-minute
soak was deliberately stopped when the feature was shelved, so it is not an
acceptance result. Do not resume recorder optimization unless the product scope
explicitly reopens it; full measurements remain in `bench-notes.md`.
This document is the ordered continuation plan for current-candidate functional
acceptance, enclosure readiness and production hardening.

## Execution Order

The batches below are intentionally ordered so destructive OTA and wired
recovery tests finish while both USB service ports are still accessible.

## E1 — Signed OTA Hardware Acceptance

Status: complete 2026-07-14.

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

Acceptance record:

- `keys/ota_signing_private.pem` has an offline USB backup and remains ignored;
- release `RC1-123-g587cd7a1` was built and packaged with key ID `rel-001`;
- P4 updated `factory / RC1-121-gb7ac66a5 -> ota_0 /
  RC1-123-g587cd7a1`; S3 updated `ota_0 / RC1-121-gb7ac66a5 -> ota_1 /
  RC1-123-g587cd7a1`;
- both targets rejected wrong signing key, wrong key ID, chip/project mismatch
  and truncated/extended bundles without changing the active slot;
- both targets survived a signed upload interrupted after 128 KiB;
- signed `ROLLBACK-TEST-*` images were rejected by the bootloader and returned
  P4 to `ota_0` and S3 to `ota_1` on the accepted release;
- final UI/touch, dual-deck playback, scratch, FLX4 input/LED and headphone-cue
  smoke passed.

## E1A — Current-Candidate Targeted Functional Acceptance

Status: matching `RC1-254-g21f21963` bench state recorded 2026-07-24. The
individual Beat FX sound/headroom, loop timing, screensaver and pull-OTA paths
have focused hardware acceptance; the remaining full-system Phase 20/profile,
USB recovery, UART integrity and endurance rows below are still pending.

Goal: prove the installed review-remediation, controller-profile, new Beat FX
and P4 display candidate on the physical P4/S3/FLX4 system before enclosure
work continues.

Deployment record:

- both `rel-001` signed bundles and the outer manifest were verified before
  upload and both targets returned HTTP 200;
- P4 moved `ota_0 / RC1-126-g812ad70f -> ota_1 /
  RC1-131-gc391e306` and returned healthy status after reboot;
- S3 moved `ota_1 / RC1-123-g587cd7a1 -> ota_0 / valid /
  RC1-131-gc391e306`, independently confirmed through P4's nested report;
- exact hashes, sizes and observations are in
  [`validation/SIGNED_OTA_RC1_131_DEPLOYMENT.md`](validation/SIGNED_OTA_RC1_131_DEPLOYMENT.md).
- P4 follow-up source commit `bd5e43ce` was packaged with both target builds as
  signed candidate `RC1-133-gbd5e43ce`; its exact verified P4 payload was
  wired-flashed to factory and passed the focused dual-waveform smoke. S3 was
  not flashed, and this wired deployment did not exercise a new OTA transition.

Tasks:

1. Smoke P4 UI/touch, media browsing/loading, dual-deck PLAY/CUE/scratch and
   Master Tempo with MAIN and FLX4 USB headphone cue active.
2. Exercise FLANGER and DELAY on CH1, CH2 and `1&2`: selector directions,
   Level/Depth, beat sizes, ON/OFF/CLEAR, mode transitions and audible tails.
   Confirm Delay's one-shot/no-feedback behavior, 1000 ms cap/fallback and the
   current Deck 1 timing source for `1&2`; confirm tempo/Beat Sync/track changes
   do not retime until another Beat FX event, and listen for unacceptable clicks
   during immediate beat-size/read-head changes.
3. Disconnect/reconnect FLX4 USB MIDI/audio under activity and confirm priority
   touch events, LED resynchronization and audio-stream recovery.
4. From an AP client, verify guarded P4/S3 web control, controller-profile and
   OTA mutations, including the expected Host and control header behavior.
5. Install/overwrite a valid FLX4 controller profile, reject corrupt and
   no-overwrite cases, confirm S3 activation/reconnect, and exercise the
   documented backup/recovery path without sacrificing the built-in fallback.
6. Capture UART startup/recovery and sustained control-link diagnostics while
   both decks and FX are active.
7. Before promoting a new baseline, deploy and boot-verify one matching P4/S3
   candidate version, then record both slots, states and versions.

Acceptance:

- no panic, watchdog, reboot, stuck touch/platter, audio drop or persistent
  queue/link error;
- UI, playback, MAIN/cue, controller input and LED state remain correct;
- Flanger/Delay behavior matches the documented target, depth, timing and tail
  semantics without unacceptable transition clicks;
- guarded mutations reject malformed/unauthorized requests and valid profile
  replacement remains recoverable;
- only after these checks and a recorded matching-version deployment may the
  selected candidate replace RC1-123 as the latest fully functionally accepted
  baseline.

## E2 — Enclosure Wiring And Service Readiness

Status: complete 2026-07-20.

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
2. [software complete 2026-07-26] Move controller-triggered UI/library work
   from the `deck` task to the LVGL/UI task context. The deck task now enqueues
   bounded commands and `ui_update()` drains at most eight per LVGL frame.
3. [host complete 2026-07-26] Validate the independent `generic_midi_ci`
   non-FLX4 fixture through compile, P4 registry match, S3 activation, semantic
   mapping, snapshot and LED output. Physical non-FLX4 acceptance remains
   required before advertising broad device compatibility.
4. [host complete 2026-07-26] Keep the deterministic five-minute dual-deck
   Master Tempo soak in the P4 host gate. It covers 48/44.1 kHz sources,
   opposite tempo offsets, pitch preservation, source-position drift,
   long-coordinate rebasing, large sample jumps and clipping. Continue the
   hardware portion for P4 CPU/I2S deadlines and listening quality.
5. [host complete 2026-07-26] Keep the reproducible headless LVGL simulator
   gate green. It builds the real P4 UI against a pinned LVGL commit, drives
   actual button callbacks and compares exact 800x480 framebuffer hashes for
   Overview D1/D2, Library, Hot Cues, Settings and the screensaver restore path.
6. Leave the one-time initial pad sweep as a low-priority cosmetic issue unless
   a specific functional MIDI/state fault becomes reproducible.

Acceptance:

- signing and recovery processes are documented for production operation;
- controller input cannot invoke heavy LVGL/library work on the control task;
- the profile platform is demonstrated with at least one non-FLX4 device;
- the deterministic dual-deck key-lock host soak remains green and the
  hardware run confirms acceptable P4 deadline margin and listening quality;
- UI navigation and rendering changes pass the pinned screenshot gate, while
  DSI/PPA motion, touch and panel timing remain hardware acceptance items;
- remaining cosmetic work does not displace functional or safety gates.

## Resume Point

Start the next hardware session by deploying one matching current candidate,
then close the remaining **E1A — Current-Candidate Targeted Functional
Acceptance** and E4 rows. E2 wiring/service readiness is already recorded
complete; follow the targeted matrix with E3 closed-enclosure endurance only
after every E1A deferral is explicit.

Keep `STARTUP_CHECKLIST.md`, `DEVELOPMENT_PLAN.md`, `DOCUMENTATION_STATUS.md`
and this plan synchronized as each acceptance gate closes.
