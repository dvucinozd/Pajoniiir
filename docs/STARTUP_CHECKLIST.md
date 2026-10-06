# P4 operation and release checklist

For the shared-core/M3 development branch, use the exact target identity and
[integration gates](SHARED_P4_CORE_INTEGRATION.md). Existing physical records
remain image-specific; successful builds do not qualify the new M3 target.

## Shared-core M3 first installation

- [ ] Freeze clean source, successful exact-SHA CI, image/bundle/ELF hashes and
  candidate evidence; keep the signed artifacts immutable.
- [ ] Identify the M3 MAC, pre-v3 silicon, COM port, old slot and old image SHA.
- [ ] Stop playback and use the [preserving migration tool](M3_SHARED_CORE_MIGRATION.md);
  save full flash, or explicitly reuse a verified installed recovery image and
  capture protected regions/NVS/OTA selection before any device write.
- [ ] Verify factory write before resetting OTA selection. Preserve bootloader,
  partitions and settings. Keep wired recovery access.
- [ ] Verify board/project/source/ELF and operator settings, then install the
  matching signed bundle and require `running_image_state=valid` in an OTA slot.
- [ ] Complete [Campaign A/B](RELIABILITY_MONITORING_PLAN.md) and operator audio,
  panel/touch/controls acceptance on this exact image. Record unrun gates explicitly.

These checks do not authorize publication or qualify JC4880/JC1060 hardware.

For the installed `M3-dev-ge4536de2ec6f` candidate in ota_1, see the
[exact-image acceptance record](validation/M3_SHARED_WAVEFORM_REGRESSION_20261006.md).
Both 11574f4e and c698fa67 failed on dual waveform deformation; c698fa67 also
showed visual stutter without audible stutter.
The measurement candidate also reproduced the visual failure and measured
57 coalesced refreshes in a short dual-playback window. Follow the
[repair/retest record](validation/M3_SHARED_WAVEFORM_REGRESSION_20261006.md) before
starting a new 60-minute campaign. Exact clean source/ELF and VALID state were
verified on 2026-10-07 after the upload helper's earlier polling deadline
expired; this later observation is not a startup-time measurement.
Its short retest also failed: solo sharp, dual deformed, audio clean, with 55
coalesced refreshes. Both decks are stopped. The full soak remains NOT RUN.
The zero-wait display-observation e4536de2 candidate passed production contention
tests, unchanged simulator baselines, all three clean builds, CI and packaging.
Its installed source/ELF and VALID state were observed within the helper deadline,
but the operator again confirmed solo sharp, dual deformed and clean sound.
The dual window coalesced 65 refreshes. Both decks are stopped; Campaign B
cannot begin before a successful measured/operator retest. See the
[diagnostic audit](validation/M3_DIAGNOSTIC_OVERHEAD_REVIEW_20261007.md) for
remaining loop-query waits and heap/UART overhead.

Status: **current M2.5 JC4880 checklist and development qualification gates**
(2026-10-06). Acceptance already recorded in the
[M2.5 release](validation/M2_5_RELEASE_20261006.md) is not a request to
repeat completed tests. Checkboxes below are procedures for operation or a
new/changed candidate; NOT RUN entries remain outside accepted scope.

M2.5 passed final tagged OTA/startup and 62.622211-second listening/telemetry
checks. Its new 180-minute soak was explicitly waived by the operator because
audio code is unchanged. This exception does not waive future changed-image gates.

## Normal JC4880 operation

- [ ] Use the accepted regulated supply and unchanged protected dual-VBUS wiring.
- [ ] Connect PCM5102A MAIN and FLX4 headphones before raising amplifier levels.
- [ ] Insert Rekordbox media in USB0 and connect FLX4 to USB1.
- [ ] Wait for display, Library and controller/profile/UAC readiness.
- [ ] Confirm the expected library and normal transport, MAIN and cue.
- [ ] LOAD LOCK is enabled by default: pause/stop the destination before replacing
  its track. A rejected load must leave its existing track intact.
- [ ] Preserve the active Library location and use explicit source-cue restore
  only when local overrides should be replaced.
- [ ] Treat unexplained reset, brownout, watchdog, missing USB root or latched
  audio loss as a failure; save diagnostics before retrying.
- [ ] Stop playback before media removal, maintenance writes or power-off.

The released previous UI is the normal presentation. Preview and recorder
controls are not production features. S3CP v2/v3/v4 profiles are accepted by
v91; bare historical M2.4 accepts v2 only. Dynamic profiles are not proof of
physical non-FLX4 support.

## Wi-Fi and signed OTA

- [ ] Enable Wi-Fi/P4 Remote in Settings when needed; connect to `Pajoniiir`
  and open `http://pajoniiir.local` or `http://192.168.4.1`.
- [ ] Expect temporary AP absence during a STA connection test or pull-update
  check; reconnect to the restored AP and verify state.
- [ ] Before OTA stop both decks, leave recorder inactive and keep power stable.
- [ ] Record project, version, slot and health; use only the correct signed
  `main-deck-p4.ddjota` for JC4880.
- [ ] After restart verify version/slot, startup readiness, empty OTA error,
  Library, FLX4 controls/LED/UAC and MAIN/cue.
- [ ] A pending image must meet critical startup readiness and, when saved-enabled,
  AP/HTTP readiness within 60 seconds. External USB devices/clients are not
  required for boot confirmation.
- [ ] If the candidate rejects or resets before confirmation, verify rollback
  to the prior image. Do not mistake OTA service `idle` for valid image state.
- [ ] Local signed push is the intentional rollback path; public pull is newer-only.
- [ ] Use wired recovery only for an identified Pajoniiir board when necessary;
  ignore COM devices from other projects. App-only OTA cannot migrate partitions.
- [ ] Keep keys/hosting secrets out of Git, logs and public artifacts.

v91 recovered automatically in recorded OTA/reboot/rollback scenarios.
The old M2.4 cold-power-cycle observation is not a required normal v91 step.
Physical unconfirmed-image rollback and 60.253-second startup rejection passed
with isolated diagnostic images, restoring the frozen release.

## Recorded v91 acceptance and remaining variants

The release record closes focused MP3/WAV/FLAC, MAIN/D1/D2 cue, scratch,
CUE/PLAY/MT, loop OUT/half/double/exit, combined scratch/MT, local cue
save/reload/reboot/delete, normal D1 LOAD lock and stopped USB reconnect.
Strict counters and operator results are tied to the exact image.

Remaining local qualification, **NOT RUN**:

- [ ] Title/downbeat comparison including an offset grid; actual playlist
  hierarchy/order, rapid artwork browsing and corrupt/missing PWV4 fallback.
- [ ] Source cue deletion/reimport, HOLD RESTORE, full persistent identity
  isolation across exports sharing a raw track ID.
- [ ] Touch/controller parity for held CUE, hot cues, jog modes and load lock,
  shifted LOAD variants and untested deck/format combinations.
- [ ] Analysis-tail/EOF/VBR seek edge cases, index-load latency and rapid
  LOAD/SEEK/UNLOAD cancellation on real media.
- [ ] Active USB removal/cancel and untested held-control reconnect variants.

The accepted soak total is 180m01.37s **in two segments**, including a saved
interrupted interval. The operator explicitly accepted that exception;
an uninterrupted 180-minute exact-v91 soak remains **NOT RUN**.
Do not carry that exception or measured passes onto another image.

## JC1060 / DDJ-400 / Link qualification — NOT RUN

- [ ] Record board revision/panel variant, flash/PSRAM, USB topology and exact SHA.
  Verify native 1024x600 touch/render, SD and Ethernet PHY/DHCP.
- [ ] Keep PCM5102A disabled (Ethernet pin overlap), ES8311 disabled for USB pacing.
- [ ] Capture DDJ-400 UAC descriptors, MAIN 1/2 and cue 3/4 isolation, full-scale
  16/24-bit packing, MIDI/LED/SysEx and UAC teardown/reconnect/restart.
- [ ] Verify stopped-only sink changes; reject transitions during active
  playback, scratch, load or recording.
- [ ] Link defaults OFF and binds only Ethernet. Check two distinct player claims,
  conflicts/reclaim, observer reason, peer loss and no local library advertisement.
- [ ] Verify single-session browse, folders/playlist order, metadata, 2,000-row
  truncation, cancel/timeouts and no stale source rows/artwork.
- [ ] Incoming LOAD uses common admission and cannot ACK/replace a deck before
  verified local audio acceptance.
- [ ] Exercise NFS downloads and full-identity audio/ANLZ/artwork/cues on named
  CDJ models and a separately named rekordbox configuration.
- [ ] Swap media A/B with identical IP/player/track ID/extension but different
  audio. B must inherit none of A's assets or local cue edits.
- [ ] Test space reserve, cache prune pins, corruption, cancel, SD/network removal
  and interrupted power. Incomplete completion manifests are never cache hits.
  Unidentified volumes require a fresh session download.
- [ ] Completed SD playback survives network/unrelated USB loss.
- [ ] Measure output phase/drift and latency for each sink/sample-rate. Engineering
  calibration is RAM-only. Test master handoff/loss, jitter/reorder, scratch hold
  and explicit PLAY/SYNC alignment; WAIT retains tempo, no periodic seek.
- [ ] Perform exact-image OTA/recovery, listening and sustained dual-deck gates.
  A green LOCKED indicator or host/mock test cannot close these checks.

Software evidence: [E](validation/FORK_IMPROVEMENTS_PACKAGE_E_SOFTWARE_20261004.md),
[F](validation/FORK_IMPROVEMENTS_PACKAGE_F_SOFTWARE_20261004.md),
[I](validation/FORK_IMPROVEMENTS_PACKAGE_I_SOFTWARE_20261005.md),
[J](validation/FORK_IMPROVEMENTS_PACKAGE_J_SOFTWARE_20261005.md),
[K](validation/FORK_IMPROVEMENTS_PACKAGE_K_SOFTWARE_20261005.md).

## Experimental storage / recorder — NOT RUN

- [ ] Use a separate experimental image; record board/card model/capacity/SHA.
- [ ] Compare SD idle wait and DMA/bounce policies in otherwise identical A/B builds.
- [ ] Measure read/write/fsync/gate wait, decode runway, strict output/USB counters,
  ring watermarks, heap/stack and listening.
- [ ] Test ring overflow (explicit recording failure), full card, removal,
  finalize/`.part`, power loss and STOP timeout/retained writer ownership.
- [ ] Verify download/recording admission exclusion; no simultaneous writers.
- [ ] Keep recorder and SD experiments disabled in ordinary packaging.

See [G procedure](validation/FORK_IMPROVEMENTS_PACKAGE_G_SOFTWARE_20261004.md).

## Before a commit or new release

- [ ] Documentation-only: run `tools/check_documentation.ps1` and `git diff --check`.
- [ ] Firmware: initialize IDF v6.0.2, run relevant/full host suites, build affected
  target(s), preserve independent locks and the `0x380000` application budget.
- [ ] UI: run real product layouts and relevant preview/Link simulator modes,
  visually review changes before updating screenshot baselines.
- [ ] Artwork fallback regressions: on a changed candidate, check real artwork remains
  visible, no-artwork tracks show the Pajoniiir logo, late cover arrival replaces
  the logo and media/page changes retain no previous cover. Empty product decks
  and folder rows must not display stale track artwork.
- [ ] Capture matching idle/loaded/active resource scenarios: allocation failures,
  internal/DMA free/minimum/largest block and critical stack floors. Missing
  evidence or failed absolute floors blocks acceptance; >10% regression requires
  explanation/correction. Do not walk the PSRAM largest-block heap during playback.
- [ ] Freeze exact clean source/configuration and applicable green CI; diagnostic
  startup fault flags must be OFF.
- [ ] Install that image and run change-driven physical/listening/lifecycle gates.
  Default final duration is uninterrupted 180 minutes unless a new explicit
  image-specific exception is recorded.
- [ ] Keep published tags/assets immutable; verify signatures and public
  hash/size after uploading versioned content, then switch the correct channel.
- [ ] Record PASS, FAIL, NOT RUN and explicit exceptions separately; do not
  advertise JC1060/non-FLX4/real peer support before physical acceptance.
- [ ] Selectively commit/push and verify local/remote SHA.

Repeat relevant qualification after power/wiring/enclosure, USB ownership,
decoder/storage/DSP/sink, network/OTA/trust or display/touch changes.
Completed historical checks and failed candidates remain in dated records;
[DOCUMENTATION_STATUS.md](DOCUMENTATION_STATUS.md) defines current scope.
