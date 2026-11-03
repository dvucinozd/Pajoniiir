# P4 operation and release checklist

For the [2026-11-03 JC1060 candidate](JC1060_COMPLETION_20261103.md), the local
operator owns the test board and will use DDJ-FLX4. Confirm device identity,
partition/security state and recovery backups before installation. Test the
ordinary image first, then the separately identified SD cold-power candidate;
keep the latter out of public pull channels. New memory-bank edits are local
NVS overrides; HOLD SOURCE restores imported memory cues, while the existing
HOLD RESTORE button restores Hot Cues. Memory PREV/NEXT require a stopped deck.
LOAD LOCK defaults ON and persists. JC1060 Wi-Fi defaults OFF; enable its own
Settings switch without changing Ethernet DJ LINK. Ethernet management remains
required for pending-image acceptance; saved enabled Wi-Fi must also become
ready. Wi-Fi OFF must retain Ethernet HTTP. Physical gates are NOT RUN until
recorded on this exact image; JC8012 is deferred and S2 stays operator-closed.

Current hold point: clean `8bc3b53a` is locally built/qualified/signed; COM3 is
confirmed but in another agent's use. Wait for the operator's availability
announcement before any serial/API/reset/backup/flash action. Start with the
[physical identity/recovery checklist](JC1060_TEST_CHECKLIST_20261103.md).

The [JC8012 source audit](reviews/2026-10-19-jc8012-source-audit.md) closes PR54's
merge/CI record, not a new hardware gate. A published Apache-2.0 panel candidate
is sequence-equivalent to the donor; it is still inactive. Do not bundle the
registry touch firmware based on its MIT package label: NOTICE excludes it.
The synthetic-wrapper syntax probe is not an installable component or firmware
acceptance. Keep IDF6.0.2 and the JC1060 SD experiment OFF in ordinary images.

For [JC board preparation](reviews/2026-10-19-jc-board-preparation.md), leave
`PAJONIIIR_SD_COLD_POWER_EXPERIMENT` OFF in ordinary builds. Its JC1060-only
candidate needs a fresh sdkconfig, explicit build/verifier admission and later
rail/pin, warm-reset and Hosted/USB hardware checks. Existing boards must select
stock IDF PPA; never add a JC8012 override to the shared component root. JC8012
has no active BSP/entrypoint or physical acceptance while touch and factory-panel
table provenance remains unresolved. Existing accepted images stay installed.

For the 2026-10-19 branch integration, `ui_next` remains a disabled demo, not a
production startup component. Numark's `deckN.pfl_set` profile requires matching
post-integration firmware; published beta2 does not implement that semantic
extension. Preserve FLX4/DDJ-400 toggle behavior. New-image Numark hardware gates
remain NOT RUN; no installation, controller swap or additional soak is requested
by this software integration. Existing accepted images and waivers remain intact.

For published [shared-p4-beta.2](validation/SHARED_P4_BETA_2_20261019.md), verify
the signed release manifest/SHA256SUMS and each board's separate source identity.
The downloaded M3/JC4880 images retain their exact accepted bytes; JC1060 is
software-verified only. Do not transfer acceptance across project/board/image
or use recovery partition files for ordinary web OTA. No new installation is
requested by publication; stable M2.5/public OTA and existing waivers stand.

For the [2026-10-19 follow-up](validation/M3_SOAK_CLOSEOUT_JC4880_OTA_20261019.md),
M3's accepted hour and retained late finding remain unchanged. JC4880
M2.5-249-g0a33ec1f is VALID ota_0 with exact source/ELF and operator GUI
confirmation. Its ordinary FLX4-selected MAIN rate is44.1 kHz with
fixed_output_sample_rate0; do not impose M3's fixed48-kHz condition. Preserve
the incomplete first monitor attempt and separate corrected smoke evidence.
Keep headphone LEVEL low initially and retain all22 strict loss checks.
The corrected60.234-s smoke is complete:93 samples/1,902 MIDI, all22 strict
deltas/late0, verified loops and CUE/PLAY on both decks, final stopped/loops OFF
and operator control/audio/display PASS. JC4880 soak remains waived, not PASS.

For the [MT fallback handoff candidate](validation/M3_MT_FALLBACK_HANDOFF_20261018.md),
`2fa22631` is VALID `ota_1`, but its loop-exit retest fails UAC drop/overflow
with a repeated 139-ms mix segment. Both decks are stopped, loops OFF. The next
mix-window DMA observer `f1c5ec9c` passes software/build/package, is VALID `ota_0`,
and completes one 60.267-s loop/CUE window with all 22 strict deltas and late
zero, operator-confirmed. Keep the stall investigation open: it did not
reproduce there, and adding observations changes timing. The following phase
sweep does reproduce PCM/UAC loss and a141-ms DMA gap; both decks are stopped.
The shared 1-KiB cache-bound candidate `1556e8d7` passes host/build/package and
is VALID `ota_1`. Replay D1 at32,023 ms / D2 at23,446 ms completes100.264 s,
three exits, all22 strict deltas/late0 and operator full-window PASS. Both decks
are stopped, loops OFF. Separate active jog/Browse pressure also passes30.145 s,
2,406 MIDI packets, all22 strict deltas/late0 and operator confirmation.
The combined60.133-s loop/control/CUE run passes all22 strict deltas/late0,
1,951 MIDI packets and operator full-test confirmation. Input largely precedes
activation and stops before exit: sustained exit-time control pressure and long
reliability remain unrun. Both decks are stopped, loops OFF. JC4880/JC1060
ordinary incremental IDF6.0.2 builds and identity/BSP/budget verifiers pass
clean `2f2b0555` with all three dependency locks unchanged. Their production code
matches installed1556e8d7; their physical acceptance remains separate. JC4880
soak is waived, JC1060 hardware deferred, hosted CI NOT RUN. The subsequent
final M3 hour completes3600.286 s /5,301 samples with all22 strict deltas zero,
unchanged image/workload and confirmed final stop. Output-late24/max12,988 us
and a sample-observed3/60-s cluster remain an investigation finding. Full-hour
operator audio/display confirmation is recorded on2026-10-19. Preserve the handoff's hashed
audit and LEVEL-change scope; this is not zero-late or full reliability PASS.
Keep exact installed image and operator evidence.
No exact masking-call attribution is implied.
Re-enable MT only after any required initial track load; avoid reloading an
already-ready same track between bounded retests. Keep all loss/admission gates
strict and the existing failed observer-image records intact.

For the [status observer repair](validation/M3_STATUS_OBSERVER_REPAIR_20261018.md),
largest-block sizes and walk timings are unavailable on all targets. Verify
explicit JSON nulls and measured free/low-water counters after candidate boot.
Clean `3459a97a` is VALID `ota_0`; GUI and live null/read-counter behavior are
confirmed. The subsequent active-control and loop-only retests both fail their
strict gates; both end stopped, loops OFF. Preserve the prior PCM2 failure and
new queue/UAC-loss records. Do not repeat playback or alter DSP speculatively;
the next bounded investigation must attribute the loop-exit mix stall.

For the [2026-10-18 FLX4 headphone candidate](validation/FLX4_HEADPHONE_LEVEL_20261018.md),
start headphone LEVEL fully down before playback and raise gradually. Dedicated
cue PCM restores 12.041 dB lost in the old transport. Verify the exact image,
LEVEL/MIX readback, both PFLs, clean headphones and unchanged MAIN on the same
track; no club-volume or electrical PASS is inferred from host tests alone.
The subsequent exact-image combined 60.125-s JC4880 MT/loop/CUE/PFL/MIX/LEVEL
smoke is operator-confirmed, with all 22 strict deltas/output-late zero and
both decks stopped. This closes that short smoke, not the waived long soak.

The [ordinary M3 candidate](validation/M3_HEADPHONE_PACKAGE_20261018.md) is now
installed as `M3-dev-g71ee534269bf`, exact source/ELF and VALID `ota_1` after
authorized local OTA. Ordinary USB roles, FLX4 and 260 tracks are confirmed;
both decks remain stopped. GUI/touch are operator-confirmed and physical knob
readback is LEVEL=0 / MIX=0 after movement. The subsequent 60-s combined smoke
aborts at 40.642 s on new D2 PCM underruns, ending stopped with loops OFF.
The operator accepts volume without perceived disturbance; retain telemetry
FAIL and investigate before a targeted retest. Successful OTA and loudness
alone do not close the combined playback-control gate.

For the authorized `shared-p4-beta.1` contributor distribution, use the
[beta record](validation/SHARED_P4_BETA_1_20261011.md) and
[JC1060 handoff](JC1060_BETA_HANDOFF_20261011.md). Verify the kit's board/project,
source/ELF, signature and hashes before any installation. New beta assets are
not installed or physically accepted merely by publication. Stable M2.5 and
public OTA channels remain unchanged; JC4880 soak is waived and JC1060 physical
testing is deferred to Kayrosen, both explicitly NOT RUN for those scopes.

The [2026-10-14 contributor review](reviews/2026-10-14-jc1060-contributor-review.md)
now records Kayrosen's separate `4e3aa2b4` bench. Warm-reset SD and USB MAIN
reconnect recovery remain physically open, and identified the required
pending-image Ethernet/HTTP repair before signed JC1060 OTA qualification.
Do not transfer that branch's
Observe/listening results to the published beta or a future integrated image.

The [selective integration follow-up](reviews/2026-10-14-jc1060-selective-integration.md)
implements USB MAIN recovery and the pending Ethernet/HTTP software gate. Before
installing its future JC1060 candidate, verify exact source/config/image identity
and test DHCP/HTTP readiness, rollback, hub root admission, loaded-track USB MAIN
reconnect and SD warm reset on that board. Software regressions alone do not
close these physical gates.

Current Hercules bench: clean `6166d936`, VALID `ota_1`, Hercules USB3/root0,
storage USB2/root1, Wi-Fi ON. Local OTA/startup and short audio/display plus
active jog/Browse smokes pass; both decks are stopped. The
[final handoff](reviews/2026-10-11-shared-core-final-handoff.md) is current authority.
The e16ecfb5 hour/lifecycle acceptance remains historical and does not transfer.
This experimental arrangement does not change ordinary FLX4 defaults.
USB1 is power-only on this bench; do not require a new serial connection for
documentation closeout. Do not repeat accepted tests solely to fill historical
unchecked boxes. Remaining individual profile and board gates stay explicit.

The [ordinary local software matrix](validation/SHARED_P4_LOCAL_QUALIFICATION_20261011.md)
passes on clean `5ff76b6c` with exact-source builds for all three boards.
This is build evidence, not a request to install its ordinary M3 image over the
accepted Hercules bench configuration. Keep configuration-specific packaging
and physical acceptance separate; hosted CI remains skipped by operator decision.

Three frozen-source ordinary signed packages and their debug/configuration
manifests now verify locally. Review the
[delivery handoff](reviews/2026-10-11-shared-core-delivery-readiness.md)
before selecting a later installation. None of these new images is installed
or physically accepted by that historical packaging step. The later controller
bench OTA/short-smoke scope is recorded in the final handoff above.

For the [S2 performance candidate](../controllers/ni_traktor_s2_mk3_full/README.md),
keep S2 on USB3 HS and the stick on USB2 FS. The operator closed S2 work after
full-hour 48/44.1-kHz playback acceptance and waived remaining physical checks.
Do not start another S2 test. Keep Wi-Fi enabled and record the exact image and
negotiated S2 rate separately from MAIN. Source mappings, host tests and waived
checks do not establish unrestricted physical acceptance. General memory and
OTA-restart records are retained in the
[closeout review](reviews/2026-10-10-s2-closeout-resources-restart.md).

The operator subsequently clarified that the historical OTA restart was
associated with moving the cable. It is closed as an operator cable-intervention
event; do not schedule a firmware repair or a repeat test for that event.

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

For the `M3-dev-gfb5cbca13f33` M3 qualification baseline in VALID
ota_0, see the
[exact-image acceptance record](validation/M3_SHARED_WAVEFORM_REGRESSION_20261006.md).
The preceding 1d3ee7b6 short retest was improved but not fully fluid: slight dual stutter remains
with and without loops; MAIN/headphones were confirmed clean. Both decks are
stopped and the original loops restored. Do not start Campaign B until the
remaining visual gate passes.
The bf2fdc47 repeated retest retained slight dual stutter with clean sound.
The fb5cbca1 short MT/zoom-8 dual retest passed with fluid waveforms and clean
MAIN/headphones sound. A subsequent short operator review confirmed all five
zooms fluid and sound clean; captured per-zoom dwell varied. Campaign B subsequently
passed on this exact image for 3600.163 seconds with operator visual/listening
confirmation, zero new PCM/UAC/service-log losses and 11 output-late monitoring
events. Both decks ended stopped. Campaign A physical cycles/operator checks
were subsequently SKIPPED BY OPERATOR. The [automatic software-reboot supplement](validation/M3_AUTOMATIC_RECOVERY_20261007.md)
observed ten recoveries with nine complete timing windows; host visibility flags
and cycle-6 timing remain investigation/incomplete evidence. This is not a
physical Campaign A PASS. [Autonomous negative OTA and injected startup rollback](validation/M3_OTA_NEGATIVE_ROLLBACK_20261007.md)
subsequently passed and restored the exact accepted image. Current-source GitHub
Actions was SKIPPED BY OPERATOR (quota), not PASS. Remaining image-specific gates
and other-board physical acceptance remain open.
[Catalog/web LOAD LOCK evidence](validation/M3_LIBRARY_CUES_WEB_LOAD_LOCK_20261007.md)
now records 324 identities stable across reboot and six refused replacements
while both decks kept playing. Final state is the same image, both decks empty
and stopped. On-device cue writes and physical MIDI/touch checks remain NOT RUN.

The [real-export audit](validation/M3_REAL_EXPORT_AUDIT_20261007.md) completed
read-only on the PC and found artist/album row-layout losses and missing EXT
Hot Cue D. Repair and rerun source comparisons before accepting a new candidate;
do not label this audit a full library PASS. No playlists/source loops occur
in this export; physical cover/cue rendering remains NOT RUN.
Correction candidate `M3-dev-g082d2c0abda0` has passed software comparisons,
host regressions, M3 build and signed identity/budget checks. Local OTA installed
it VALID in ota_1, with the exact source/ELF and 324 tracks; all 13 artist names
are recovered. D1 France Gall and D2 Jura Stublić are READY/stopped for the
operator's cue/rendering check. The operator subsequently confirmed D1 A–D
recall, with source D at 123347 ms observed through the API. Subsequent
[cue lifecycle checks](validation/M3_CUE_LIFECYCLE_20261007.md) passed local
override, deletion and restore-source persistence through reboot. Both decks
are stopped and the original source bank is restored. Broader new-image
physical acceptance is separate.
Ordinary FLX4 and touch LOAD refusal on each playing deck subsequently passed,
with unchanged tracks and clean sound; see [physical record](validation/M3_PHYSICAL_LOAD_LOCK_20261007.md).
Selected [stopped-deck routing cases](validation/M3_PHYSICAL_LOAD_ROUTING_20261007.md)
also pass; both decks are finally READY/stopped with Star Eater.
Subsequent touch D1 and FLX4 D2 completed ordinary stopped LOAD parity.
Repeated [Shift+Load playing-deck refusals](validation/M3_PHYSICAL_SHIFT_LOAD_LOCK_20261007.md)
passed on both decks; final tracks are Combat Ready, both stopped.
The subsequent `4620b83c` screensaver repair is now installed VALID in ota_0.
France Gall D1 and Jura D2 are READY/stopped for the real two-minute idle wake
check. [Physical wake acceptance](validation/M3_CONTROLLER_SCREENSAVER_WAKE_20261007.md)
failed: first PLAY started playback. Atomic UI handoff candidate `af4ca590`
is now installed VALID in ota_1; operator PLAY wake/next-PLAY checks pass with
one consumed PLAY wake recorded by API. Browse and touch regressions also pass;
final counter is two consumed controller wakes and both decks are stopped.
final soak is deferred and earlier image-specific tests do not transfer.
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

The ee54f0d1 loop-observation repair passed host qualification, unchanged product
simulators, M3 build and signed packaging. The operator reports worse visual
stuttering; it is not physically accepted. During this diagnosis, perform only
M3 candidate builds and bench tests. JC4880/JC1060 builds and the full CI matrix
resume after an operator-accepted M3 retest. Cancelled/interrupted work is not a
PASS. The separate bench manifest retains full software qualification as false;
it does not relax final integration, Campaign A/B or publication gates.

The initial completed-frame candidate 6d55ce2a failed; the bench was returned to
ee54f0d1 VALID in ota_0 before installing b0c66744 VALID in ota_1. Deferred
preservation reduced the solo copy cost but b0c66744 also failed: severe stutter
began with dual playback. Its interrupted dual aggregate is not a continuous
dual measurement. The next revision fixes retained position handling and adds
RAM observation-age counters. Before its physical
PASS, verify exact source/ELF/VALID state,
`capabilities.display_framebuffers=2`, stable panel/touch/colours and all zooms,
then compare solo/dual MT timing including framebuffer-sync/present cost.
Require unchanged playback state throughout each window; an operator stop
invalidates that window and terminates the test. Compare observation-age
counters as well, without assuming mutex contention caused the device failure.
Both previous visual failures remain recorded; no physical PASS follows from
installation or host pixel tests. Check audio/UAC and HTTP stability as well.

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
