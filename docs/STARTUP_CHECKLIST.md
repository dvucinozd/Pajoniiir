# P4 Startup and Release Checklist

Status: **active P4-only checklist, reconciled 2026-09-20**.

## Repository and build

- [x] Canonical branch is `master`; feature completion was merged by `d3099f9`.
- [x] Post-merge source head `c786de7` matches `origin/master`.
- [x] UAC idle-continuity remediation is committed as `b9139e1`.
- [x] ESP-IDF v6.0.2 is the only supported SDK.
- [x] Complete P4 host suite passes.
- [x] Clean `build_signed` and signed-bundle verification pass.
- [x] `dependencies.lock` is tracked and unchanged by the exact build.
- [x] Install and focused-smoke immutable tagged `M2` on hardware.
- [x] Repeat automated host/UI/build/artifact gates after merge; GitHub Actions
  run `35523213652` passes at `c786de7`.

## Latest installed exact-image evidence

- [x] `M2` installed on `ota_0` with empty OTA error.
- [x] USB0 mounts and exposes the current 324-track Library.
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
- [x] Operator confirmed the accepted wiring is already in its approximately
  two-month enclosure configuration. A separate rerun is waived; repeat after
  any wiring, supply or enclosure change.

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
- [x] Implement and self-test the accelerated paired Group I/J harness with
  one load/play setup, ten independent evidence records and fail-fast cleanup.
- [x] Give every successfully primed UAC stream a monotonic epoch and scope the
  one-shot underflow grace to that stream without hiding other loss classes.
- [x] Exact-image active reconnect acceptance: I1 and I2 passed; MIDI, LEDs,
  UAC, dual playback and audible MAIN/cue recovered with clean fault deltas.
- [x] Held-control reconnect acceptance: J1 jog-touch release passed.
- [x] Close Groups I/J by explicit operator decision: I3--I5 and J2--J5 are
  waived, are not counted as PASS and will not be resumed.
- [x] Implement and self-test the guarded Group K software-reboot endpoint and
  deterministic K1/K2 evidence harness. The endpoint requires OTA idle, both
  USB roots healthy and both decks stopped; a power cycle cannot satisfy it.
- [x] Confirm two exact-image software reboots recover both roots without manual
  reinsert: K1/K2 passed on boots 424 and 426.
- [x] Complete Group L OTA-reboot gates. The final matrix accounting is 43/50
  PASS, 7/50 waived and 0 pending, with 39 accepted physical actions.
- [x] Implement and self-test the deterministic Group L signed push-OTA harness;
  require a verified exact-version bundle, opposite-slot boot and automatic
  recovery of both roots before playback/operator acceptance.
- [x] Confirm two OTA reboots recover both roots without manual reinsert: L1
  changed `ota_1 -> ota_0` on boot 427 and L2 returned `ota_0 -> ota_1` on boot
  428, both with clean dual-deck and operator checks.

## Media and audio

- [x] Verify physical MP3, WAV and FLAC fixtures by real USB0 playback.
- [x] Run complete natural-EOF playback for PCM16/44.1 kHz WAV and
  96 kHz/24-bit FLAC with zero PCM underruns and BNA recovery.
- [x] Run 30-second simultaneous MP3+FLAC and MP3+WAV counter windows; both
  windows have zero locked-read, PCM-underrun, late and BNA deltas.
- [x] Run mixed MP3/WAV/FLAC dual-deck load, seek, loop, CUE, scratch and EOF
  across the focused media, functional and combined-soak gates.
- [x] Confirm BNA recovery and locked-backend-read counters remain acceptable;
  accepted windows completed with zero gated delta.
- [x] Accept exact-image dual Master Tempo deadline evidence from the focused
  regression and 180.156-minute combined soak. Direct cycle-margin profiling
  remains uncaptured and must be repeated after audio/DSP scheduling changes.
- [x] Reproduce and fix the immediate dual-Master-Tempo `IDLE0` WDT; exact
  `RC2-150-g909e068` MP3 + 96 kHz FLAC smoke exceeded the old reset point with
  normal sound and zero underrun/UAC-loss counters.
- [x] Remove the per-sample 64-bit timeline seqlock cost and verify the exact
  `RC2-151-g838c254-dirty` MP3 + 96 kHz FLAC fixture for 184 seconds with zero
  PCM/UAC/WDT errors and clean listening.
- [x] Fix active-loop Shift+Jog search freezing at near EOF; post-OTA D2 reaches
  natural EOF and restarts normally with clean counters and sound.
- [x] Listen for clicks, flat-top clipping, pitch artefacts and MAIN/cue defects;
  operator confirmed clean accepted runs.
- [x] Exercise both decks with Master Tempo off/on and near-EOF scratch/hold.
- [x] Complete Beat FX routing/transition/tail coverage required by the
  accelerated M2 functional gate.

## Wi-Fi, web, profile and OTA

- [x] Move the canonical pull-channel root from the retired
  `pajoniiir.zadar.click/ota` path to `https://ota.pajoniiir.eu` in the
  publisher, URL regression and active OTA procedure. Deployment credentials
  remain outside Git and firmware.
- [x] Publish `latest.json` and the versioned signed bundle, then verify both
  public HTTPS paths from a network outside the Pajoniiir captive AP. The
  public `M2` bundle is 2,459,708 bytes and matches SHA-256
  `f5620858e9983f8272eceb4d3dc93afee7b906cc6e8335e8280b1ceed5bcf9a5`.
- [x] Re-smoke hardened pull OTA AP-to-STA-to-AP transition on the remediated
  exact image: the probe and longer check both restored the AP without reboot.
  After DNS propagation, the device read the public HTTPS channel and reported
  `already running this build`; boot 12 and dual-USB health stayed unchanged.
- [x] Prove the RC2-to-M2 positive pull path: install the signed
  `RC2-156-gd2dabfa` bridge on `ota_1`, receive the public `M2` offer, then
  download, authenticate and boot `M2` on `ota_0`; boot 14 restored USB0 and
  FLX4 MIDI/UAC with no OTA error or new TWDT.
- [x] Verify newer-only, offer TTL, size, SHA-256 and signature rejection paths
  through the complete host suite; live M2 channel recheck also refused a
  reinstall as `already running this build`.
- [x] Interrupt a declared full upload after 131,072 image bytes and verify
  `HTTP upload interrupted`, unchanged `M2 / ota_0 / boot 14`, live API and
  healthy USB0/FLX4.
- [x] Verify signed push rollback and opposite-slot boot: the RC2 bridge booted
  on `ota_1` as boot 15, then public pull restored `M2 / ota_0 / boot 16`.
- [x] Verify guarded control, load, seek and profile mutations. Unguarded
  control/profile POSTs returned 403; guarded identity loads, dual PLAY,
  D1/D2 seeks and STOP completed with zero strict counter delta. An existing
  profile without overwrite returned 409, while an explicit valid FLX4
  overwrite returned 200 and remained active after boot 16 -> 17.
- [x] Verify USB0 and FLX4 recover automatically after two signed OTA reboots.

## Product soak and enclosure

- [x] Define the accelerated M2 beta functional and combined-load soak harness.
- [x] Run the 45--60 minute combined functional session on the candidate image;
  the accepted run continued for 78.176 minutes with clean strict counters and
  operator-confirmed uninterrupted, artifact-free MAIN/cue audio.
- [x] Reproduce and repair the later combined-soak UAC idle-continuity failure:
  pre-fix first-cycle reproduction failed; the installed repair passed 126
  complete focused CUE/restart transitions with zero strict counter delta.
- [x] Run the automated three-hour combined-load soak on the repaired candidate:
  180.156 minutes, 60 operations, zero strict counter delta, no reboot/TWDT and
  operator-confirmed clean audio throughout.
- [x] Include seeks, restart, Master Tempo, scratch, loops, Hot Cues, FX, MIDI,
  LEDs, MAIN/cue, USB cache and web status traffic.
- [x] Require no reset, brownout, lost mount/controller, latched control or
  gated error-counter increase.
- [x] Complete the final exact-image operator smoke on M2 boot 17: physical
  dual PLAY, both jog wheels, D1/D2 CUE/PFL and Library browse/LOAD passed;
  MAIN/cue audio was clean and the correlated strict counters stayed at zero.
- [x] Dedicated closed-enclosure power/temperature rerun waived by operator
  after approximately two months of operation in the existing enclosure;
  numeric thermal margin remains uncaptured.
- [x] Existing enclosure operation and Pajoniiir AP access accepted by operator;
  no separate RF/strain qualification is planned for this scope.
- [x] Accessible wired recovery/service path confirmed by operator.

Automated and unattended M2 evidence:
[`validation/M2_AUTOMATED_RELEASE_GATE_20260920.md`](validation/M2_AUTOMATED_RELEASE_GATE_20260920.md).

## Production and release

- [x] Use one shared service password; shared-credential risk explicitly
  accepted for this product scope.
- [x] Define signing-key custody: encrypted offline primary storage plus a
  separate encrypted offline backup; never commit or publish either copy.
- [x] Provision encrypted offline primary and separately stored encrypted
  backup copies of the signing key; operator confirmed completion on
  2026-09-20 without exposing private key material.
- [x] Close backup recovery for the M2.1 release by explicit operator decision:
  encrypted primary and separately stored backup copies are confirmed, while a
  disposable recovery-signing test is accepted as deferred future maintenance
  and is not claimed as executed evidence.
- [x] Define the M2.1 signing-key rotation boundary: `rel-001` remains trusted;
  planned rotation installs a successor trust key while `rel-001` is still
  available, while emergency replacement uses wired recovery.
- [x] Decide Secure Boot, Flash Encryption, PMF/WPA3 and irreversible
  provisioning policy: WPA2/WPA3 transition mode with PMF capability is in
  source; Secure Boot, Flash Encryption and security eFuse burns are deferred
  because there is no spare P4 board on which to qualify irreversible recovery.
- [x] SBOM explicitly waived for M2.1; retain dependency lock/provenance gates.
- [x] Freeze the final commit and rerun automated gates; `M2.1` identifies
  `70824d24`, and GitHub Actions run `35532529630` passed.
- [x] Build, sign, verify and install the exact final image; M2.1 runs from
  `ota_1` and the signed bundle hash is recorded in the release evidence.
- [x] Run the final exact-image product smoke; dual-deck counters stayed clean
  and the operator confirmed audio, display and touch.
- [x] Publish hashes, slot/version evidence and remaining acceptance results in
  `validation/M2_1_PRODUCTION_RELEASE_20260920.md`.
- [x] Publish GitHub Release `M2.1` with the signed OTA bundle, wired-recovery
  binary, manifest and signature; download all four assets again and verify
  their recorded SHA-256 values.
- [x] Merge the release-qualified M2 beta successor into `master`; completed by
  `d3099f9` without moving the immutable `M2` tag.
- [x] Select production version `M2.1`.
- [x] Tag the frozen production commit as `M2.1`, build/install/smoke the exact
  tagged image, then push the immutable tag and publish its record/channel.

Security policy: [`SECURITY_PROVISIONING_POLICY.md`](SECURITY_PROVISIONING_POLICY.md).

Detailed procedure: [`migration/P4_DUAL_USB_NEXT_SESSION.md`](migration/P4_DUAL_USB_NEXT_SESSION.md).

Accelerated beta path:
[`M2_BETA_ACCELERATED_RELEASE_PLAN.md`](M2_BETA_ACCELERATED_RELEASE_PLAN.md).
