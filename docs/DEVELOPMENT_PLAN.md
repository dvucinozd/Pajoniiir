# P4 post-release development plan

2026-11-03 [JC1060 completion](JC1060_COMPLETION_20261103.md): JC8012 is deferred.
The operator has a JC1060 and will qualify it with DDJ-FLX4 after the complete
software candidate is prepared. Scope includes memory cue editing/recall,
persistent LOAD LOCK, optional Wi-Fi alongside Ethernet, playlist touch and
Link source-return behavior. Run local host/UI and all-three-board builds;
keep SD cold power separately admitted and OFF in ordinary images. Then qualify
USB MAIN reconnect without LOAD, SD reset behavior, Ethernet/Wi-Fi and signed
OTA/readiness/rollback on the exact candidate. Real Link peers and long timing
remain separate physical gates. No intermediate Actions runs or public update.
The later entry supersedes older next-action/Wi-Fi-policy statements below.

2026-10-19 [JC8012 source audit](reviews/2026-10-19-jc8012-source-audit.md):
PR54 is merged, with green thirteen-job migration CI on head and master.
A pinned published Apache-2.0 panel candidate matches all 204 donor commands;
prepare its notice-preserving board-local import separately. The MIT touch
package explicitly excludes firmware rights. Its wrapper has a passing
synthetic-only IDF6.0.2 syntax probe, while the published manifest requires6.1.
Keep IDF6.0.2 and all existing defaults; resolve firmware distribution or decide
a separate provisioning contract before implementing the actual touch port.

2026-10-19 [JC board preparation](reviews/2026-10-19-jc-board-preparation.md):
the JC8012 private PPA generator and compile/configure isolation contracts are
ready for a future licensed BSP. Both the GSL3680 payload and factory JD9365
table remain excluded; do not enable a fourth target until replacements or
distribution terms are established. JC1060 SD cold power is implemented and
software-tested behind an OFF-by-default explicit experiment. Kayrosen's
exact-image hardware gate is still open; do not change JC1060 Wi-Fi defaults.
The three production builds remain on stock PPA, with unchanged locks.

2026-10-19 branch integration: qualified shared-core PR51 is merged into
`master` without changing its production tree. The separate integration branch
imports `ui_next` as a disabled demo and adapts Numark with absolute PFL levels,
durable delivery and real profile/USB stream regressions. Existing controllers
retain momentary-toggle PFL. This is software integration, not new physical
acceptance or an OTA request. Keep published beta2 and installed images intact.
JC8012 licensing/PPA isolation and optional JC1060 SD/Wi-Fi work remain separate;
do not merge their donor implementations wholesale. See the
[branch review](reviews/2026-10-19-remaining-branches-merge-review.md).

2026-10-19: [shared-p4-beta.2](validation/SHARED_P4_BETA_2_20261019.md) is published
with the exact accepted M3/JC4880 bytes and software-verified JC1060 candidate.
Sixteen public downloads and222 ZIP inventory entries verify, along with all
signatures/identities. No firmware rebuild or additional hardware test is part
of delivery. Stable M2.5/public OTA, S2 closeout, JC4880 soak waiver and deferred
JC1060 physical gates remain unchanged. Continue only separately justified
open-gate work; do not repeat accepted M3/JC4880 blocks merely for publication.

2026-10-19: [final-hour review and JC4880 follow-up](validation/M3_SOAK_CLOSEOUT_JC4880_OTA_20261019.md)
complete the offline M3 late assessment with its finding retained. Keep M3 on
the accepted1556e8d7 image. JC4880 clean0a33ec1f builds/signs and installs VALID
ota_0; its60.234-s mixed-rate MT/loop/CUE/control smoke passes all22 strict
deltas/late0 and operator confirmation. Input precedes the automatic loop
transitions; sustained exit pressure is not qualified. No new
soak, speculative firmware change or public publication is scheduled here.
The following dated investigation stages retain their original scopes.

The [2026-10-18 MT fallback handoff](validation/M3_MT_FALLBACK_HANDOFF_20261018.md)
has a deterministic production-callback reproduction and minimal consumer-clock
repair. Host qualification and ordinary M3 incremental build/package pass;
`2fa22631` is VALID `ota_1`. Its loop-exit retest fails on UAC drop/overflow and
another 139-ms mix segment, despite no operator symptom. Both decks are stopped.
The mix-window I2S DMA observer `f1c5ec9c` passes host/build/package and is VALID
`ota_0`; its first 60.267-s loop/CUE retest passes telemetry and operator review.
The next phase sweep fails at20.192 s with D2 PCM/UAC loss and a141-ms DMA gap
across the mix window. Extend the existing HS1-KiB task-context cache policy
to ordinary P4 builds, then replay the failing source positions/exit phase.
The ordinary-path bound is RED before and GREEN after repair. Clean `1556e8d7`
passes host/build/package and is VALID `ota_1`; its100.264-s three-exit replay
passes all22 strict deltas/late0 and full operator audio/display confirmation.
An isolated30.145-s jog/Browse run also passes all22 strict deltas/late0 with
2,406 MIDI packets and operator control/audio/display confirmation. Retain both
bounded scopes: a subsequent60.133-s combined loop/control/CUE run also passes
all22 strict deltas/late0 and operator confirmation with1,951 MIDI packets.
Input is concentrated before/around loop activation, not loop exit. Sustained
exit-time input pressure, long reliability and exact causal attribution remain
open. JC4880/JC1060 ordinary IDF6.0.2 incremental builds and board verifiers
now pass clean `2f2b0555`, with unchanged dependency locks and production code
identical to installed1556e8d7. No JC physical acceptance or hosted CI is inferred.
Leave installed M3 unchanged. Its subsequent final timing soak completes
3600.286 s /5,301 samples with all22 strict deltas zero, mixed-rate dual MT/loops
and healthy FLX4 FS44.1-kHz streaming; both decks end stopped. Output-late24,
max12,988 us and a sample-observed3/60-s cluster retain an investigation finding.
Full-hour operator audio/display confirmation is recorded on2026-10-19; the
output-late investigation remains open. Headphone LEVEL changed near
861 s (74 MIDI packets); no sustained active-control pressure is inferred.
See the handoff's final-hour audit and hashed records. This closes the telemetry
observation window, not exact causal attribution or all reliability gates.
Clock tests
do not close UAC timing or admission gates; preserve all failures and criteria.

The [2026-10-18 status observer repair](validation/M3_STATUS_OBSERVER_REPAIR_20261018.md)
removes confirmed unbounded TLSF walks from the shared observation path.
Software qualification and the single ordinary M3 build pass; clean `3459a97a`
is installed VALID `ota_0`. An active-control retest fails downstream admission,
and a separate zero-MIDI loop-exit retest fails UAC drop/overflow with one slow
mix segment. Both are safely stopped and archived. Next investigate output-mix
callback work versus preemption at loop exit, separately from controller queue
pressure. Do not claim the preceding PCM2 cause solved by the observer repair.

2026-10-18: prioritize the [FLX4 headphone-level repair](validation/FLX4_HEADPHONE_LEVEL_20261018.md)
before JC4880 OTA. A production-stream regression reproduces a 12.041-dB
dedicated cue loss; the candidate restores unity after the mixer while retaining
the legacy USB MAIN mirror level. Full host qualification, JC4880 build and signed
OTA pass; clean `605bcd20` is VALID `ota_1`. The operator's corrected final answer
accepts loudness in the short solo-D1 bench test. A later combined 60.125-s
dual-deck MT/loop/CUE/PFL/MIX/LEVEL smoke also passes all 22 strict deltas,
output-late zero and operator confirmation of controls/audio/display. Actual
club use remains separate; the JC4880 long-soak waiver is unchanged.

The operator authorized shared-core PR #46 delivery and GitHub prerelease
`shared-p4-beta.1`. See the [beta record](validation/SHARED_P4_BETA_1_20261011.md)
and [JC1060 handoff](JC1060_BETA_HANDOFF_20261011.md). JC1060 physical testing
was deferred to Kayrosen; JC4880 long soak is explicitly waived, NOT RUN.
The following dated checkpoints retain their own source and acceptance scope.

The [2026-10-18 ordinary M3 headphone package](validation/M3_HEADPHONE_PACKAGE_20261018.md)
builds and signs clean `71ee5342` with unchanged qualified production code and
ordinary board policy. A later authorized local OTA confirms exact identity,
VALID `ota_1`, FLX4 and 260 tracks. The subsequent MT smoke stops after 40.642 s
on new D2 PCM underruns during the loop-clear window. The operator accepts
headphone volume and reports no disturbance, but the telemetry gate fails.
Investigate loop-exit refill and diagnostic observer timing before a targeted
retest; preserve the failed evidence. Both decks are stopped, loops OFF. No
other board build or public update is part of this step.

The [2026-10-14 JC1060 contributor review](reviews/2026-10-14-jc1060-contributor-review.md)
is complete on frozen `00041a0d`, with scoped hardware evidence on `4e3aa2b4`
and production-boundary defect reproductions. Its recommended next step was
selective integration with repairs/regressions; no new candidate or beta is qualified
by that review. Published-beta JC1060 acceptance remains NOT RUN.

The [selective integration follow-up](reviews/2026-10-14-jc1060-selective-integration.md)
implements R01-R08 software repairs and DDJ-400 loop mappings. R09's unsafe HIL
helper is excluded. Its verification ledger tracks the ordinary build/host/UI
gates separately from pending exact-image JC1060, SD warm-reset and live Link
acceptance. No existing beta, S2 closeout or JC4880 soak waiver is replaced.

The [final development handoff](reviews/2026-10-11-shared-core-final-handoff.md)
is the current delivery/open-gate authority. Installed clean 6166d936 is VALID
ota_1 on the Hercules role-swap bench; short audio/display and active-control
smokes pass. Latest ordinary three-target local builds/simulators pass on
b913818; physical acceptance is image-specific, CI stays operator-skipped and
S2 remains operator-closed. Next is PR reconciliation and separately authorized
push/merge/publication. Earlier progression below retains its dated scope.

Current shared-core work is reconciled in the
[2026-10-11 phase/P01–P10 review](reviews/2026-10-11-shared-core-hercules-closeout.md).
The Hercules source/fixture/support-scope review is complete: unchanged clean
`e16ecfb5` has exact-image final playback acceptance, with individual pad/FX/LED
limits still explicit.

The [subsequent ordinary local qualification](validation/SHARED_P4_LOCAL_QUALIFICATION_20261011.md)
now passes on clean `5ff76b6c`: complete host gate, nine simulators, both keylock
policies and all three IDF 6.0.2 builds with unchanged locks and valid budgets.
Configuration-specific signed delivery provenance is now complete. The
[delivery-readiness ledger](reviews/2026-10-11-shared-core-delivery-readiness.md)
and local PR description are prepared for review. Next work requires selecting
its scope: separate experiment builds or a
board-specific release gate. Real-media/physical acceptance remains separate;
hosted CI stays skipped.
The operator clarified that the historical OTA restart was associated with
moving the cable. That event is closed as operator cable intervention, without
a firmware-fix claim or further scheduled investigation. Earlier
installed-state and next-action paragraphs below are historical progression.

The [S2 performance candidate](../controllers/ni_traktor_s2_mk3_full/README.md)
uses the shared runtime for 80 inputs/26 outputs and experimental HS UAC2.
S2 work is closed by explicit operator decision after exact-image full-hour
48/44.1-kHz physical playback acceptance. Remaining physical checks are waived,
not PASS; do not schedule further S2 tests. Earlier diagnostic evidence remains
scoped to its image/settings. General memory evidence remains described in the
[closeout review](reviews/2026-10-10-s2-closeout-resources-restart.md).

Shared-core/M3 implementation is tracked in
[the integration ledger](SHARED_P4_CORE_INTEGRATION.md). Portable code now lives
under `firmware/p4-core`; hardware providers live under `firmware/boards`.
This development branch does not extend the physical scope of M2.5.

The shared-core implementation retains donor library/transport/profile features
and accepted M3 BSP/DSP/UI policies in one runtime. Finish software variant gates,
then use [M3 wired migration](M3_SHARED_CORE_MIGRATION.md) on the frozen candidate.
New-image physical gates include GUI/touch, dual-deck MAIN/PFL, all five zooms,
signed OTA/startup/rollback, 30 Campaign A cycles and at least 60 minutes of
worst-case Campaign B timing. Legacy-image acceptance does not transfer.
Unavailable hardware is NOT RUN. Public board-channel publication requires
separate authorization after that board's acceptance.

The accepted M3 qualification baseline is `M3-dev-gfb5cbca13f33` from clean source
`fb5cbca13f33d21dfb4e16e0ba667c340399eb0c`, installed VALID in ota_0.
It removes an unused blocking audio-position read from per-refresh jog-mode
polling. Host qualification, unchanged M3 simulator hashes, M3 build and signed
identity/budget checks passed. Its short dual MT/zoom-8 retest passed with
operator-confirmed fluid waveforms and clean MAIN/headphones sound. A subsequent
short review confirmed all five zooms fluid and sound clean. The same installed
image then passed a continuous 3600.163-second Campaign B soak: telemetry and
operator-confirmed stable/fluid display and clean MAIN/headphones. Strict new
PCM/UAC/service-log losses were zero; 11 output-late events remain monitoring
findings, not a zero-late claim. Campaign A's 30 physical cycles and operator
checks were excluded by the operator. The [automatic supplement](validation/M3_AUTOMATIC_RECOVERY_20261007.md)
observed ten software-reboot recoveries, with nine complete measured cycles,
host visibility investigation flags and one incomplete timing window. This does
not establish physical Campaign A acceptance. [Autonomous negative OTA and injected
startup rollback tests](validation/M3_OTA_NEGATIVE_ROLLBACK_20261007.md) subsequently
passed and restored the exact accepted image. Current-source GitHub Actions is
SKIPPED BY OPERATOR (quota), not PASS; full hardware acceptance remains open.
Short per-zoom dwell coverage is
recorded separately from the completed zoom-8 soak.

[Catalog/cue software and web LOAD LOCK checks](validation/M3_LIBRARY_CUES_WEB_LOAD_LOCK_20261007.md)
subsequently passed on the unchanged image: 324 persistent identities remained
equal across reboot and six playing-deck replacements were refused. Production
NVS adapter error/corruption tests now join the host runner. Physical cue/NVS,
MIDI/touch LOAD LOCK and independent real-export/UI acceptance remain open.

The [real-export audit](validation/M3_REAL_EXPORT_AUDIT_20261007.md) now
reproduces lost artist names on 13 tracks, lost album names on 12 tracks and
missing source Hot Cue D from EXT on track 7. Correct row-specific name layouts
and transactional EXT cue enrichment, preserve local cue identities, and rerun
the actual 324-track comparisons before an M3 candidate build. No firmware was
changed by the audit. Playlists/source loops are absent from this medium;
their real-media gates and on-device rendering remain open.
Correction `082d2c0a` passes exact-source 324-track comparisons with zero import
differences, the default shared host gate, final focused regressions and the
M3 IDF 6.0.2 build/signed package. It is now installed VALID in ota_1, and the
live 324-track library recovers all 13 artist names. D1 A–D recall is now
operator-confirmed, including source D at 123347 ms in the stopped API snapshot.
The [on-device cue lifecycle](validation/M3_CUE_LIFECYCLE_20261007.md) now passes:
local override, deletion tombstone and restore-source persist through reboot.
Final source D recall is 123347 ms and both decks are stopped.
Next: remaining new-image physical gates; do not transfer fb5 acceptance.
Ordinary playing-deck LOAD refusal now also passes through FLX4 and touch on
both decks; see [physical LOAD LOCK limits](validation/M3_PHYSICAL_LOAD_LOCK_20261007.md).
Selected [stopped-deck LOAD routing](validation/M3_PHYSICAL_LOAD_ROUTING_20261007.md)
also passes ordinary FLX4 D1, touch D2 and FLX4 Shift+Load D1/D2.
Subsequent ordinary FLX4 D2 and touch D1 passed, completing stopped LOAD input
parity. Repeated [playing Shift+Load refusal](validation/M3_PHYSICAL_SHIFT_LOAD_LOCK_20261007.md)
also passed on both decks; the initial disagreement is preserved and has since
been explicitly attributed to operator error by the operator.
Current bench image: clean `4620b83c`, VALID ota_0, repairing the lost direct
controller screensaver wake callback via the shared event sink. Default host
tests, M3 build and signed packaging pass; [physical wake retest](validation/M3_CONTROLLER_SCREENSAVER_WAKE_20261007.md)
failed when first PLAY started playback. The atomic UI handoff follow-up awaits
retest. Atomic candidate `af4ca590` now passes default host/M3 build/signature
checks and is installed VALID in ota_1. Operator PLAY wake/next-PLAY checks pass;
API records one consumed PLAY wake and a second Browse wake. Browse and touch
regressions also pass on af4ca590; final state is both decks stopped.
Soak remains the final step; other-board builds are deferred until
the M3 result. Prior physical cue/LOAD evidence remains tied to 082d2c0a.

The preceding bf2fdc47 skipped unchanged cover comparisons and hidden Library styling, and separated
chrome/UI-tail/LVGL timings. Host qualification, unchanged M3 simulator hashes,
M3 build and signed identity/budget checks passed. Its repeated physical test
still showed slight dual stutter with clean MAIN/headphones sound.

The preceding 1d3ee7b6 was installed VALID in ota_0.
Host qualification, unchanged M3 simulator baselines, the M3 build and signed
package verification passed. Its physical retest showed much improved visuals
with slight residual stutter and clean MAIN/headphones sound. Continuous dual
playback without loops also retains the small stutter. The full visual and
long-soak gates remain open.

The preceding b0c66744 deferred framebuffer-preservation repair passed host qualification,
unchanged M3 simulator baselines and the M3 IDF 6.0.2 build. The operator stopped
the test because of severe stuttering, beginning only when both decks played.
That image failed the physical visual gate; listening was not qualified.

During unresolved M3 bench diagnosis, build, sign and test M3 only. Resume
JC4880/JC1060 builds and full cross-board CI after the operator accepts the M3
repair. Full integration/release requirements remain mandatory afterwards.
The redundant matrix runs were cancelled; the earlier JC4880 build had already
finished and the JC1060 configure was interrupted. Keep separate M3 bench
evidence rather than marking full software qualification complete.

The paired-buffer image 6d55ce2a failed its physical test and the bench was
returned to the signed ee54f0d1 image, VALID in ota_0. Its captured solo window
spent 6.04 ms copying old framebuffer content before redraw. The subsequent
dual interval lost HTTP access and persistent logs recorded audio/UAC losses.
The installed b0c66744 revision keeps complete-frame ownership but defers preservation until
after rendering, copying only previous damage not covered by new writes.
Exact bitmap coverage preserves paused content and holes between rectangles;
fully redrawn strips require no old-pixel copy. Pixel reference tests and M3
simulator baselines, exact M3 build and signed packaging passed. The physical
retest failed. The next revision changes only UI position freshness handling:
retained observations cannot rebase the display prediction backwards. RAM
observation-age counters will distinguish this reproduced host defect from
unproven device causes. Audio DSP and panel configuration remain unchanged.
See the candidate record for the pre-v3 contract and image-specific evidence.

The preceding `M3-dev-ge4536de2ec6f` was installed in M3 ota_1 with VALID health state,
verified against its exact clean source and ELF on 2026-10-07 within the
installation helper's polling deadline. The preceding 180b008c helper's expired
deadline remains a separate historical finding.
[The current candidate record](validation/M3_SHARED_WAVEFORM_REGRESSION_20261006.md) separates
the signed OTA, 324-track catalog and mixed-rate load evidence from remaining
operator audio/UI and USB campaigns. Initial wired migration and earlier GUI/
touch/settings confirmations remain in the image-specific
[95e installation record](validation/M3_SHARED_CORE_INSTALL_20261006.md).
Both 11574f4e and the installed c698fa67 ordering repair failed on
operator-confirmed dual waveform deformation. The latter also showed visual
stutter while audio remained clean. The measurement candidate reproduced the
failure: 57 coalesced refreshes in about 12 seconds of dual playback. The next
repair, now installed after all three builds and exact-source CI passed,
removes duplicate PPA cache maintenance and unchanged frame resampling;
see [the regression record](validation/M3_SHARED_WAVEFORM_REGRESSION_20261006.md).
The installed repair also failed the physical retest: solo sharp, dual deformed,
audio clean, 55 coalesced refreshes in the short dual window. Investigate the
remaining blocking UI status reads before another candidate; the full timing
gate must restart after a successful exact-image physical retest.
The installed e4536de2 candidate shares one zero-wait, UI-owned audio-status observation per
deck between position, session-checked duration and status/progress rendering.
Busy decode retains the last observation; transport decisions keep their
authoritative APIs. Production mutex-contention and simulator recovery tests
and all three clean builds passed, but its physical retest still failed: solo
sharp, dual deformed, audio clean, with 65 coalesced refreshes. The next ee54f0d1
candidate removes the remaining waveform-phase loop-query waits, but its operator
report still rejects the visual result. The
[diagnostic-overhead audit](validation/M3_DIAGNOSTIC_OVERHEAD_REVIEW_20261007.md)
separates quiet service-file logging from recurring heap inspection and UART
producer reports; the full soak remains NOT RUN.

Status: **M2.5 published; A-L merged into master; hardware extensions open**
(2026-10-06).

## Current baseline

The accepted release is `M2.5` from
`20f1c3f04a615209ae25e9bbdae649d0f5b44e8d`, for JC4880/FLX4 with PCM5102A
MAIN and FLX4 cue. The previous product design is retained. The
[release record](validation/M2_5_RELEASE_20261006.md) preserves the explicit
final-image soak waiver, exact artifacts and focused physical scope.

Packages A-L are software verified and merged into `master`. The
[integration ledger](FORK_IMPROVEMENTS.md) records modules adapted from
[kayrozen](https://github.com/kayrozen) and their test/provenance history.
JC1060/DDJ-400/Ethernet Link is a development configuration, not released
hardware support. The recorder, SD experiments and optional UI preview remain
off in regular builds. APTA is outside this integration.

Physical rollback and the 60-second startup rejection passed using isolated
diagnostic images. They restored the original v91; those later diagnostic
commits are not the hardware-qualified release image. Ordinary packaging
rejects both fault-injection flags. Original tags and versioned assets remain
immutable.

## Next work

M2.5 JC4880/FLX4 is published after exact-source CI, final tagged OTA/startup
and operator-confirmed short audio acceptance. Subsequent maintenance uses
change-driven regressions; the remaining local-library cases below stay open.
JC1060/DDJ-400/Link, experimental recording and APTA need not block a release
that explicitly excludes their production support. Web Profile Builder v4
deployment is required only if its public exporter is part of the release scope.

The user-supplied Pajoniiir logo is now the released M2.5 artwork fallback for
loaded tracks and Library rows. It is stored as native 34x34/40x40 RGB565
constants; the existing real-artwork worker/cache remains authoritative.
Candidate `M2.4-111-ge09e9ca4` has passed signed OTA, operator-confirmed logo
display and a 62.419-second dual-deck listening/resource check; see the
[candidate record](validation/M2_5_DEFAULT_ARTWORK_20261006.md).
Real cover, missing cover and normal page/media return passed on the logo
candidate. Final M2.5 passed 62.622211 seconds of clean dual playback and
startup/resource checks. Its new long soak was explicitly waived; v91 remains
immutable and its earlier evidence is not relabeled as M2.5.

| Work | Required evidence / boundary |
| --- | --- |
| JC4880 maintenance | Change-driven regressions on a new exact image; do not rebuild current master and call it the accepted v91 |
| Remaining local-library/transport variants | Real title/downbeat, ordered nested playlists, artwork/PWV4, source-cue restore/reimport and cross-media identity; advanced touch/shift and active-removal cases remain NOT RUN |
| JC1060 bring-up | Board/panel revision, flash/PSRAM, touch/render, USB root roles, SD, RMII PHY/DHCP, memory/stack/deadline measurements |
| DDJ-400 and other controllers | Real descriptors, MIDI/LED/SysEx/reconnect and MAIN/cue channel isolation; profile fixtures alone do not qualify hardware |
| Link I/J | Real CDJ and separately named rekordbox version; discovery claims/conflicts, ordered browse, NFS downloads, cancel/media-swap/cache/space/power-loss cases |
| Link K | Measured output phase, drift, master handoff and listening per sink/sample-rate; LOCKED alone is insufficient |
| SD/recorder experiments G | Exact-card A/B latency, ring overflow, finalize/removal/full-card/power-loss, simultaneous audio deadlines and listening; separate experimental result |
| Web Profile Builder | Coordinated v4 exporter already has software evidence; deployment/acceptance belongs to that repository and is not implied by firmware publication |
| Security maintenance | Per-device credentials if exposure expands, disposable backup signature check, successor-key overlap and sacrificial hardware-rooted protection pilot |
| LIBAPTA | Separate branch/program; revalidate upstream and entry gates in [its plan](LIBAPTA_P4_INTEGRATION_PLAN.md) |

JC1060 must keep PCM5102A disabled because its pins overlap Ethernet;
ES8311 is disabled in the USB-only configuration. No Wi-Fi Link or local
NFS/DBServer library server is part of this program.

## Maintenance rule

Repeat gates because behavior or assumptions changed, not because records aged.

| Change area | Minimum checks |
| --- | --- |
| Documentation/media | Documentation integrity, whitespace, links; visual Pages review for manual layout changes |
| Tool/profile format | Relevant suites; parser/compiler/storage/upload compatibility and generated fixtures together |
| Shared firmware | P4 host runner, affected IDF v6.0.2 target builds, lock stability and image budget |
| UI | Actual presentation simulator layouts, visual review, exact-image display/touch and active playback checks |
| USB/storage/audio/OTA | Relevant focused hardware regression, counters, operator sound and lifecycle checks |
| DSP/scheduling/buffering | Reproduce/fix the failure; exact-image active timing and appropriately long audible test |
| Partition/bootloader/trust key | Wired recovery plan, isolated signed build and supervised installation |

Shared-core changes require all three entrypoint builds. Use committed independent
dependency locks and the fixed `0x380000` image budget. Preserve actual source,
configuration, version, slot/boot, strict deltas and operator results.
A host soak or simulator cannot replace physical deadlines or sound.

## Release discipline

- Freeze an exact clean source and configuration; run applicable automated gates.
- Install and accept that exact image for a named board/controller/sink/peer set.
- Default duration gate remains an uninterrupted 180-minute dual-deck run.
  The recorded v91 segmented exception is image-specific, not an automatic
  waiver for later releases.
- Publish immutable signed artifacts before atomically switching the correct
  board channel; independently verify public size/hash/signature and assets.
- Record NOT RUN and explicit exceptions separately from PASS.
- Never move published tags or overwrite frozen assets.
- Do not use fault-injection, preview, recorder or storage-experiment images as
  ordinary production releases.
- Keep private keys and hosting credentials outside Git/logs/artifacts.
- No eFuse operation is implicit in release tooling.

## Historical plans

The [M2.4 record](validation/M2_4_PRODUCTION_RELEASE_20260929.md),
[M2.5 handoff](M2_5_HANDOFF_20261002.md),
[H migration history](validation/FORK_IMPROVEMENTS_PACKAGE_H_PROGRESS_20261004.md)
and [L hardware ledger](validation/JC4880_L_CANDIDATE_20261005.md) retain
older installation, supply, resource and audio-failure sequences. Their former
"next step", installed-version and unpublished-channel statements describe
those sessions; current status is [DOCUMENTATION_STATUS.md](DOCUMENTATION_STATUS.md).
Do not reopen completed v91 gates without a new failure or changed assumption.
