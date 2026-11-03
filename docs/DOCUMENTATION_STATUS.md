# Documentation status

Status: **current documentation authority, updated 2026-11-03**.

The [JC1060 completion candidate](JC1060_COMPLETION_20261103.md) adds local
memory cue/loop editing and recall, persistent ordinary-Settings LOAD LOCK,
optional JC1060 WPA2 APSTA alongside Ethernet, touch playlist opening and Link
source-return refresh. The operator now has a JC1060 and selected DDJ-FLX4 for
qualification. JC8012 work is deferred. Software verification and exact package
identities are recorded in that candidate record; physical installation,
reconnect, SD, networking and rollback gates remain NOT RUN. Wi-Fi stays OFF
on fresh JC1060 NVS; Link remains Ethernet-only/default OFF and SD cold power
is a separate OFF-by-default experiment. Existing releases and installed-image
acceptance are unchanged. GitHub Actions is not dispatched during development,
following the operator's budget instruction. Earlier dated progress below
retains its original image and policy scope.

Final clean `8bc3b53a` passes full local host qualification (144 build/206 run
steps, no optional SKIP), all nine UI presentations/145 exact hashes, all three
ordinary IDF6.0.2 builds and the separately configured SD variant. Both JC1060
signed packages, a 51-file signed delivery inventory and source recovery verify.
The operator confirms COM3 but reserves the board for another agent and will
announce availability. Device access and all physical gates remain NOT RUN;
do not open/reset/probe it before release. No push, Actions or public update.

PR54 is merged into `master` as `37d5bddb`; its reviewed head and merge both
pass all thirteen migration jobs, including SD-power. Master also passes
documentation, Pages and separate dual-USB workflows. The
[JC8012 source audit](reviews/2026-10-19-jc8012-source-audit.md) finds a published
Apache-2.0 panel-table candidate whose 204 commands/data/delays equal the donor.
The registry MIT touch wrapper passes a synthetic-payload syntax-only probe on
IDF6.0.2, but its NOTICE excludes firmware rights and its manifest requires
IDF6.1. The actual payload differs from the donor in two values. No payload,
driver, BSP or framework upgrade is imported by this research; touch distribution
and all new-board hardware gates remain open. Production defaults and installed
images are unchanged.

The [JC board preparation](reviews/2026-10-19-jc-board-preparation.md) isolates
the JC8012 PPA preparation from all three production targets and implements
an opt-in JC1060 first-mount SD power sequence. Clean `5a005092` passes full
host qualification, all three ordinary IDF6.0.2 builds and a fresh-config
JC1060 SD-experiment build. Existing targets retain stock PPA; locks are
unchanged. The new CI SD-power variant is wired but not yet hosted-verified.
JC8012 has no active BSP/entrypoint: both the touch payload and the factory
panel table have unresolved distribution terms and are excluded. JC1060 SD
hardware acceptance remains NOT RUN; its Wi-Fi policy is unchanged. No OTA,
installed firmware, beta2 asset or public channel changes follow from this work.

PR53 repairs the UI mock on master `5b09a1d8`: all twelve migration CI jobs
pass, including the host/UI screenshot gate and eleven firmware configurations.
Documentation, Pages deployment and the separate dual-USB workflow pass; all
four automatic workflows are successful on that exact SHA. Locally, nine presentations /143
hashes retain their exact baselines. Production code matches clean tested
`abffb203`; no firmware installation, signing or release asset change follows.

PR52 merges UI/Numark integration into master `5666c608`. Its automatic CI
passes eleven firmware jobs, documentation, Pages and dual-USB checks, but the
migration host job fails on a missing PFL-state definition in the UI mock.
The follow-up adds that definition only; all nine simulator presentations
pass unchanged screenshot baselines locally. Production code/build evidence
is retained; hosted harness-repair verification remains separate. The failed
workflow is not relabeled. Beta2 asset sizes/digests are unchanged. Python's
OTA certificate error is followed by a successful default-validation Schannel
curl fetch: the public metadata bytes retain their original SHA-256. No
insecure bypass or channel update is performed. See the
[integration record](reviews/2026-10-19-branch-integration.md).

The [branch integration](reviews/2026-10-19-branch-integration.md) merges the
qualified shared core through PR51, imports new UI as an explicitly disabled
prototype and adapts Numark with absolute PFL levels and real regressions.
Final clean `abffb203` passes full host qualification and ordinary IDF6.0.2
build/identity/isolation checks for M3, JC4880 and JC1060, with unchanged locks
and stock PPA. The new Numark mapping requires newer firmware than beta2;
new-image hardware/signing/OTA remain NOT RUN. Automatic CI on baseline master
`d878c066` now passes all four workflows, including twelve migration jobs and
Pages deployment; this is scoped to that SHA. Existing installed acceptance,
JC4880 soak waiver and JC1060 physical gates remain unchanged. JC8012 and old
JC1060 whole-donor merges remain excluded.

The [remaining-branches merge review](reviews/2026-10-19-remaining-branches-merge-review.md)
freezes all remote development tips and the local APTA branch. It recommends
integrating the qualified shared branch into master first, retaining new UI as
a disabled prototype, repairing Numark PFL/test wiring, and splitting JC8012
from inherited donor repairs, the PPA override and unresolved touch-driver
provenance. The JC1060 donor is already selectively adapted; APTA is ancestral.
Virtual merges and focused software checks do not qualify new hardware. This
review performs no merge, branch deletion, firmware change or publication.

[shared-p4-beta.2](validation/SHARED_P4_BETA_2_20261019.md) is now published and
independently verified as a non-Latest GitHub prerelease. Sixteen public assets
match their sealed originals;222 ZIP entries, all signatures and exact
board/source/version/ELF/configuration records verify. Retained accepted M3 and
JC4880 binaries are unchanged; JC1060 remains software-only pending Kayrosen.
The three embedded versions/sources intentionally differ under one distribution
tag. Stable M2.5, beta1 assets and public latest.json are unchanged. Hosted CI
remains NOT RUN under the quota waiver; no new physical acceptance is inferred.

The [final-hour offline review and JC4880 OTA](validation/M3_SOAK_CLOSEOUT_JC4880_OTA_20261019.md)
complete the saved-journal interpretation without modifying production firmware
or repeating M3's accepted hour. The selected same-boot window has no new
>=50-ms outlier; rate-limited phase/DMA context does not identify an exact cause.
Ordinary JC4880 `M2.5-249-g0a33ec1f` passes incremental IDF6.0.2 build, board
verification and signed packaging, then a single HTTP200 OTA confirms VALID
ota_0 and operator GUI acceptance after host DHCP restoration. Its short smoke
is recorded separately; the first monitor attempt incorrectly imposed M3's
48-kHz MAIN policy on JC4880's selected44.1-kHz rate and is incomplete, with all
strict deltas unchanged. The corrected60.234-s/93-sample smoke records1,902 MIDI
packets, all22 strict deltas/late0, confirmed loop/CUE/PLAY actions and final
stopped/loops OFF. The operator confirms controls, clean MAIN/FLX4 headphones
and fluid waveforms. MIDI precedes automatic loop activation/exit, so sustained
exit-time pressure is not inferred. All101 saved sample/action/boundary status
snapshots pass independent offline audit. No public release/channel or JC soak
waiver changes.

The [MT fallback handoff repair](validation/M3_MT_FALLBACK_HANDOFF_20261018.md)
reproduces a separate same-generation stale-clock defect: ordinary resampling
advances the consumer, then MT resumes its old cursor and rewinds it. The real
engine callback fails the new host regression before repair; all 224 cases
pass after re-anchoring to the actual playhead. Host qualification, ordinary M3
incremental build and signed packaging pass. Clean `2fa22631` is VALID `ota_1`.
Its 43.137-s loop-exit retest fails on 11 UAC dropped blocks / 2,251 overflow
frames and a 142,503-us output-late (139,212 us in one mix group); other 20 strict
deltas are zero. Both decks are stopped, loops OFF; operator reports no symptom
or control movement. CUE phases were not reached. The bounded mix-window DMA
observer `f1c5ec9c` passes host/build/package and is VALID `ota_0`. Its first
60.267-s loop/CUE retest has all 22 strict deltas and output-late zero, with
operator audio/display/transition confirmation. No outlier reproduced: this
does not establish its cause or a timing repair. A subsequent phase sweep fails
at20.192 s on D2 PCM +1,546, UAC drop +13 / overflow +2,318 and output-late142,477 us.
New DMA context records one completion /141,430-us gap during mix141,841 us,
supporting interrupt starvation rather than attributing all time to MT work.
The ordinary path lacks the existing HS1-KiB task-context cache bound; extending
that shared policy is RED/GREEN host-reproduced. Clean `1556e8d7` passes host
qualification, M3 build/package and is VALID `ota_1`. Its exact-source-position
replay completes 100.264 s /142 samples and three loop exits with all22 strict
deltas and output-late zero; the operator confirms the full audio/display/exit
window. Both decks are stopped, loops OFF. This closes the short targeted replay,
not long reliability or exact causal attribution. A separate30.145-s active
jog/Browse test records2,406 MIDI packets, all22 strict deltas/late0 and operator
confirmation of both controls/audio/display. Both decks are stopped. Combined
controls/loop/CUE run then completes60.133 s /83 samples /1,951 MIDI packets,
all22 strict deltas/late0 and operator full-test PASS. Input mostly precedes
loop activation and stops before loop clear; sustained exit-time pressure and
long qualification remain unrun. Earlier admission/PCM/UAC failures remain
preserved. Both decks are stopped, loops OFF. JC4880/JC1060 ordinary IDF6.0.2
incremental builds and board verifiers pass clean `2f2b0555` (production code
identical to installed1556e8d7), 2,609,952 /2,668,480 bytes respectively; all three
dependency locks unchanged. See the
[build matrix](validation/evidence/2026-10-18-shared-cache-builds/matrix.json).
Neither JC image is installed or physically accepted here; JC4880 soak stays
waived, JC1060 hardware stays deferred to Kayrosen and hosted CI is NOT RUN.
The subsequent final exact-image M3 soak completes3600.286 s /5,301 samples:
all22 strict counters remain unchanged throughout, final identity stays clean
1556e8d7 VALID ota_1, both decks stopped, healthy FLX4 FS44.1-kHz streaming.
Both MT/loops and mixed source rates remain active throughout sampling.
Output-late24/max12,988 us includes a sample-observed3/60-s cluster, so the
classification is complete_with_output_late_investigation, not zero-late or an
unconditional reliability PASS. On2026-10-19 the operator confirms clean MAIN
and FLX4 headphones, fluid waveforms and stable display for the entire hour;
the later operator-confirmation record preserves this separately from the
original audit. Headphone LEVEL changes around860–861 s account for74 observed MIDI
packets; this is not an untouched-controls claim. The
[all-sample audit](validation/evidence/2026-10-19-m3-cache-bound-soak/audit.json)
and hashed compressed raw responses preserve that scope. Earlier long-soak
NOT RUN statements above describe the earlier short-test stage. Nothing is
published; open JC physical, hosted CI and remaining shared-core gates stand.

The [shared status observer repair](validation/M3_STATUS_OBSERVER_REPAIR_20261018.md)
removes unbounded internal/DMA largest-block walks from ordinary as well as HS
memory snapshots. Free/low-water counters remain measured; fragmentation and
walk timings are explicitly unavailable, without weakening resource thresholds.
Full host qualification, ordinary M3 build, board/budget checks and signed
packaging pass. Clean `3459a97a` is installed VALID `ota_0`; live unavailable
memory fields and GUI are confirmed. The active-control retest stops at
25.968 s on 19 runtime admission failures; a separate zero-MIDI loop-only
retest stops at 42.186 s on 13 UAC dropped blocks/2,320 overflow frames and a
142,441-us output-late event concentrated in one mix segment. PCM deltas are
zero on both retests. Both end stopped, loops OFF. The operator reports no
disturbance and sufficient headphone volume; both telemetry FAILs remain.
The observer defect is repaired, but loop-exit mix timing and controller
admission remain open. The prior PCM2 failure is preserved, not reclassified.

The [ordinary M3 headphone-repair package](validation/M3_HEADPHONE_PACKAGE_20261018.md)
is prepared locally as clean `M3-dev-g71ee534269bf`, project `main-deck-m3`.
The fresh ESP-IDF 6.0.2 build, ordinary board/component/budget verifier and signed
bundle/manifest packaging pass. Effective configuration matches the preceding
ordinary M3 build; the shared production/test/tooling code matches qualified
`605bcd20`. FLX4 USB2 / storage USB3, PCM5102A MAIN, accepted display/touch and
Wi-Fi defaults are retained. A subsequent explicitly authorized local OTA
uploads the same bundle with HTTP 200 and confirms exact source/version/ELF,
VALID `ota_1`, idle OTA service, FLX4 MIDI/non-faulted 44.1-kHz UAC and 260
tracks. Both decks remain stopped and the historical crash summary is unchanged.
The operator confirms GUI/touch; live LEVEL=0 / MIX=0 readback matches the
reported physical knobs after movement.
A later operator-ready mixed-rate MT smoke stops at 40.642 s on 3,387 new D2
PCM underrun counts while awaiting loop-clear completion; the other 21 strict
deltas and output-late remain zero. Both decks end stopped, loops OFF. The
operator confirms no perceived disturbance and sufficient headphone volume.
Volume is accepted for this window; the incomplete combined smoke is a
telemetry FAIL. Loop-exit refill versus diagnostic observer timing remains
unresolved; the 436,751-us heap-query wall time does not prove causation.
Evidence is archived without a firmware change, rerun or public publication.

The [FLX4 headphone-level investigation](validation/FLX4_HEADPHONE_LEVEL_20261018.md)
reproduces a 12.041-dB dedicated headphone loss in the former JC4880 `c7d2e9cd`
PCM-paced FLX4 transport. The repair restores dedicated cue unity and exposes
headphone LEVEL. Clean `605bcd20` is now VALID `ota_1`; full host qualification,
focused USB policies, the JC4880 build/board checks and signed packaging pass.
After an operator-confirmed power interruption of the first upload, a controlled
retry confirms startup with the identical package and unchanged historical crash
summary. A 30.016-s solo-D1 listening window records 136 MIDI packets, all 22
strict deltas/output-late zero and both decks stopped. The operator corrects the
initial answer and confirms sufficient loudness with ATH-M20x. The expectation
that 100% would suit a club is not a club test or measured full-level sweep.
A later 60.125-s / 86-sample mixed-rate dual-deck MT/zoom-8 smoke passes with
3,658 MIDI packets, all 22 strict deltas/output-late zero and confirmed loop/CUE
isolation. MIX spans CUE..MASTER and LEVEL reaches approximately 91.1%. The
operator confirms both jogs/Browse, PFL D1/D2, MIX/LEVEL, clean MAIN/headphones
and fluid waveforms. The exact-image short combined smoke is closed; both decks
end stopped, loops OFF. No soak or public update occurred. M3 now contains the
repair on `71ee5342`; its earlier physical acceptance below retains its own scope.

The [ordinary M3 integration OTA and FLX4 smoke](validation/M3_SHARED_INTEGRATION_OTA_SMOKE_20261018.md)
installs clean `c0ee8152` as `M3-dev-gc0ee81527b4d`, VALID `ota_0`. The ordinary
FLX4 USB2/FS/root1 and storage USB3/HS/root0 roles are restored; 260 tracks,
PCM5102A MAIN and FLX4 44.1-kHz headphones are available. The operator confirms
GUI/touch. One 60.532-s mixed-rate, pitch-zero/MT-OFF smoke receives 2,100 MIDI
packets; all 22 strict deltas and output-late remain zero. Both-deck loop/CUE
transitions and the offline all-sample audit pass; both decks end stopped.
The operator confirms both jogs/Browse, clean MAIN/FLX4 headphones, fluid
waveforms and disturbance-free loop/CUE transitions, closing this exact-image
short smoke. No new build, long soak, push, public beta or channel update occurred.

The [JC1060 contributor kit](JC1060_CONTRIBUTOR_KIT_20261015.md) is ready locally
for Kayrosen on the already verified clean `c0ee8152` BIN/ELF. The signed OTA
bundle/manifest, signed complete kit inventory and signed ZIP checksum verify.
ZIP extraction and 11 offline integrity/source-recovery cases pass, along with
10 existing signing tests and OTA release helper tests. The kit contains the
short combined checklist, recovery boundaries, matching DDJ-400/FLX4 profiles,
debug/flash artifacts, source bundle and an unsent handoff draft. No firmware
was rebuilt, installed or physically qualified; no message, push, public beta
or OTA-channel update occurred. SD warm reset and the candidate's hardware/
startup/rollback gates remain open.

The [JC1060 selective integration follow-up](reviews/2026-10-14-jc1060-selective-integration.md)
implements the confirmed software repairs and four DDJ-400 loop bindings in
the canonical shared core. Its verification ledger distinguishes ordinary
software builds/regressions from NOT RUN exact-image JC1060, SD warm reset,
signed OTA and live Link acceptance. Unsafe donor HIL/Docker tools are excluded.
Published beta assets remain unchanged; S2 closeout and JC4880 soak waiver stand.

The [completed integration software verification](reviews/2026-10-14-jc1060-integration-verification.md)
binds clean `c0ee8152` to all three ordinary ESP-IDF 6.0.2 builds, board/isolation/
budget and linked restart checks, full required-fixture host qualification with
no SKIP, four unchanged UI baseline gates, and Windows/Ubuntu dependency-byte
regressions. Hashes and original host evidence are retained. The subsequent M3
installation is scoped above; the other images are not installed or physically
accepted by this verification. JC1060 SD, hub, audio reconnect and signed
OTA gates remain open. Full Linux firmware builds and hosted CI remain NOT RUN.

The [completed JC1060/DDJ-400 contributor review](reviews/2026-10-14-jc1060-contributor-review.md)
audits frozen branch `00041a0d` against local `78dc3b7d`, preserves all 71 changed
file hashes and reproduces root-admission, HS UAC1 cadence, partial web CUE,
loop-EOF and terminal output-sink issues at documented production boundaries.
The DDJ-400 v4 binary compiles identically and passes parser/storage/map checks.
Dependency hash differences are exactly LF/CRLF, with no version change.
Kayrosen's exact `4e3aa2b4` JC1060/DDJ-400 results provide scoped physical and
long Observe evidence; seven long-window counters are not the current all-22
TimingSoak gate. Warm-reset SD and USB MAIN recovery remain failed/open;
signed OTA is NOT RUN. At review completion, selective integration still
required repairs and canonical production code was unchanged. The subsequent
software repairs are scoped above; published beta assets remain unchanged.

The [JC4880 controller lifecycle candidate](validation/JC4880_CONTROLLER_LIFECYCLE_20261012.md)
is now installed as clean `M2.5-221-gc7d2e9cd`, VALID `ota_0`, with Hercules
MIDI/44.1-kHz UAC and the 260-track library available. Its 150.359-s short
mixed-rate playback passes all 22 strict deltas and output-late delta zero;
the operator confirms both audio paths and display. A 90.438-s supplement
receives 48 MIDI packets with all 22 strict deltas/output-late zero; the operator
confirms both-deck Pad FX 1/2 pads 1-4 press/release, observed colours/VU and
audio/display. On 2026-10-14 the same image passes the 30.203-s / 74-sample
global FX supplement: 16 MIDI packets, all six ordered effect/beat/ON/OFF
state changes, all 22 strict deltas and output-late zero. The operator confirms
each command and both audio/display paths. The requested selected Pad FX,
observed LED/VU and six-command global FX block is closed for JC4880 `c7d2e9cd`;
other effect DSP types and broader profile qualification remain separate.
Both decks are stopped and global FX is OFF. Public beta assets/channels
remain unchanged; this is not a new-image soak or reconnect qualification.

The [2026-10-12 controller lifecycle software follow-up](reviews/2026-10-12-controller-lifecycle-review.md)
reproduces and repairs stale unaccepted PLAY/LOAD/jog work after disconnect and
LED packets crossing an output connection generation. It adds partial snapshot,
callback-race, bounded LED headroom/retry and symmetric D1/D2 LOAD/PLAY regressions.
Full required-fixture host qualification (no SKIP), a fresh ordinary M3 build
under ESP-IDF v6.0.2 and its board/isolation/budget verifier pass. The build is a
working-tree development artifact, not an installed clean release.
The selected joint Hercules Pad FX/global FX/LED/VU control block is now closed
on the exact JC4880 image recorded above. The
subsequent JC4880 installation is separately scoped above; existing beta assets
and historical physical acceptance retain their exact image boundaries.

Manual revision 2026-10-12: [GitHub Pages user manual](index.html) now covers
JC4880/JC1060/M3 port/audio/network policies, FLX4/Hercules/S2/DDJ-400 support
limits, profile setup, screensaver first-event wake and board-specific signed
beta installation. Stable M2.5 instructions and all five original screenshots
remain. Desktop/mobile QA checks board/controller/display/update sections,
anchor targets, loaded images and page overflow; mobile tables scroll locally.
M3 APSTA service behavior is distinguished from JC4880 temporary STA visits.
Publication is through the existing Pages workflow, separate from firmware CI.

The [2026-10-12 shared library follow-up](validation/SHARED_LIBRARY_EXPORT_AUDIT_20261012.md)
passes ten focused host suites plus delayed-artwork and two-export cue isolation
regressions. A read-only audit of the FAT32 260-track export confirms all metadata,
three flat playlists/69 entries and 257 valid DAT/EXT/PWV4 pairs. It also retains
missing music/analysis/cover files and inconsistent France Gall analysis headers
as source-data findings, not an overall media PASS. Real folders/memory loops
are absent; the operator deferred wider physical checks and requested retaining
software/audit results only for now. Published beta firmware remains unchanged.

The operator authorized PR #46 push/merge and GitHub beta `shared-p4-beta.1`.
The English README is reconciled while preserving all original image embeds and
operational instructions. The [beta record](validation/SHARED_P4_BETA_1_20261011.md)
tracks immutable packaging/publication; the [JC1060 handoff](JC1060_BETA_HANDOFF_20261011.md)
defines deferred contributor testing. Existing stable releases/channels remain
unchanged. Publication does not transfer historical physical acceptance.

`shared-p4-beta.1` is now published as a GitHub prerelease from merged source
`40e72fcbf403a1650185a4c64509dd11bba9f73f`. Fresh ordinary builds and signed
packaging for all three boards pass; all fifteen downloaded assets hash-match,
and stable M2.5/public OTA metadata are independently unchanged. The beta record
contains exact identities and verification. No new beta asset is installed or
physically accepted; current hosted CI remains operator-skipped.

[JC4880 Hercules FS FIFO repair](validation/JC4880_HERCULES_FIFO_20261011.md)
records the installed shared-core baseline, operator-confirmed GUI/touch/Browse,
and the reproduced 400-byte FIFO/admission limit rejecting Hercules's 540-byte
endpoint. Full host suite, JC4880 build, board verifier and signed local OTA now
pass on clean `e22ff334`. `M2.5-209-ge22ff334` is VALID in `ota_1`, and Hercules
automatically opens healthy 44.1-kHz packed-24 UAC1. The 30.516-s combined smoke
passes all 22 strict deltas and output-late delta zero with 2424 MIDI packets;
both decks ended stopped. Operator confirms GUI, controls, MAIN and waveforms;
headphone sound was separately confirmed after enabling the previously off PFL.
This is short-smoke scope, not full qualification. Only JC4880 was built for
the initial OTA repair; its subsequent matrix follow-up is recorded below.
No publication or wider qualification is implied.

The subsequent 60.140-s JC4880 functional block passes all 22 strict deltas,
output-late delta zero and 546 MIDI packets. Operator confirms MT/opposite pitch,
loops, seek, CUE-to-PLAY, PFL/Mix/Volume and both audio/display paths. A fresh
ordinary matrix and UI simulators pass on `95c1befb`; the subsequent M3-HS
physical-root admission correction has passing targeted regressions, with its
final three-target builds and full required-fixture host qualification now PASS
on clean `722bb976`, with no SKIP. Eight root/FIFO policies and JC4880/M3-HS
production stream tests are covered. Installed JC4880 remains `e22ff334`;
final matrix images were not flashed. Hosted CI and wider physical gates remain
open. The final handoff now separates this bench from the last verified M3.

Operator disposition on 2026-10-11: the JC4880 long soak is explicitly waived,
SKIPPED BY OPERATOR / NOT RUN, without changing its accepted short-test scope.
JC1060 hardware is unavailable to the operator; contributor Kayrosen will test
it later. Its physical acceptance remains NOT RUN pending exact-image evidence.
These are recorded dispositions, not additional physical PASS results.
The later contributor evidence is reconciled in the 2026-10-14 review above;
it does not qualify the published beta or a future integrated candidate.

[Final development handoff](reviews/2026-10-11-shared-core-final-handoff.md)
consolidates implementation, software results, installed identity and outstanding
acceptance/publication gates. The 6166d936 active-control supplement now passes
20.531 s / 35 samples with 2421 MIDI packets, all 22 strict deltas zero and no
new output-late events. Operator confirms both jogs/Browse, audio outputs and
waveforms normal; both decks stopped. This closes the short-smoke input gap,
not full new-image qualification. S2 stays operator-closed with waivers.

[Authorized M3 OTA and short smoke](validation/M3_FINAL_CONTROLLER_CANDIDATE_20261011.md#authorized-local-ota-and-short-smoke)
installed `M3-dev-g6166d9362730`, clean source `6166d93627304895857b626e719032de3b4f884d`,
as VALID `ota_1`; exact ELF identity and unchanged historical crash summary were
verified. Hercules MIDI/UAC and the 260-track library returned. A 30.094-s
mixed-rate dual smoke passed all 22 strict deltas and output-late delta zero,
with operator-confirmed GUI/touch, clean MAIN/headphones and fluid waveforms.
Controls were not exercised during playback (NOT RUN); both decks ended stopped.
First PLAY only woke screensaver as designed. Two historical D1 underruns remain
unresolved observations, not a demonstrated wake defect. No full new-image
qualification, S2 retest, push or public release was performed.

[Final local M3 controller candidate](validation/M3_FINAL_CONTROLLER_CANDIDATE_20261011.md)
is built and signed on clean source `6166d93627304895857b626e719032de3b4f884d`,
version `M3-dev-g6166d9362730`. The 2,605,008-byte application / 2,605,196-byte
bundle pass experimental board, signature, identity, configuration and ELF audits.
Both Hercules and full S2 profile artifacts are included and match their tracked
binaries. Its SDKCONFIG header matches the accepted Hercules bench configuration,
with controller USB3 and storage USB2. At packaging it was not installed or
accepted; the subsequent local OTA/short-smoke scope is recorded above. It is
not publicly published. S2 remains operator-closed with waivers; no S2 tests were
reopened and no stored profiles or device controls were changed.

[Final local three-target matrix](validation/SHARED_P4_FINAL_BUILD_MATRIX_20261011.md)
passes on clean source `b913818052de99af61c3a8c752b814185794b50e` with new
ESP-IDF 6.0.2 build directories and dedicated default SDKCONFIGs. JC4880,
JC1060 and M3 images are 2,608,224 / 2,666,912 / 2,601,600 bytes; all board,
source identity, common-component isolation, lock, linked-wrapper and resampler
checks pass. Three product UI simulators pass without baseline changes.
This closes that local software matrix. Current CI and physical acceptance of
these new images remain unrun; installed firmware and frozen packages are unchanged.

[PR 46 full protocol-directory review completion](reviews/2026-10-11-pr46-dj-link-protocol-completion.md)
finishes the operator-authorized manual inspection of all 45 tracked files in
`common/djlink` and `common/dj_link_core`. Four additional confirmed defects
(full-width name overread, premature master handoff, nonaffirmative ACK and NFS
backoff overflow) have repairs and failing-before/passing-after regressions.
Full host qualification has no SKIP; Linux ASan/UBSan and the ordinary M3
ESP-IDF 6.0.2 build/verifier pass (2,601,280 bytes). Documentation/provenance
is corrected and dependency locks remain unchanged. This completes that manual
review scope, not CodeRabbit service review, real Pro DJ Link interoperability
or shared-core acceptance. Installed firmware and frozen packages are unchanged.

[PR 46 inherited DJ Link protocol review](reviews/2026-10-11-pr46-dj-link-protocol-review.md)
records the operator-authorized manual review after CodeRabbit rejected the
service request. Reproduced DBServer streaming tag and incomplete path-response
defects are repaired with production client regressions. The ordinary M3
build/verifier passes at 2,601,280 bytes; dependency locks, experimental defaults,
installed firmware and frozen packages remain unchanged. This is not a completed
CodeRabbit review or real Pro DJ Link interoperability PASS.
Full host qualification passes without SKIP, the final focused client gate
passes, and Linux ASan/UBSan protocol/network regressions pass. Documentation
integrity and `git diff --check` pass.

[PR 46 recorder/DJ Link review](reviews/2026-10-11-pr46-recorder-link-review.md)
covers four service requests (24 returned reviewed-file entries, five issues),
all addressed locally, plus a reproduced recorder ring sequence-wrap defect.
Repairs cover STOP ordering, oversized blocks, checkpoint documentation, LOAD
queue contention and donor provenance. The common protocol scopes contained
only changed README/UPSTREAM files; inherited C code is not service-reviewed by
this batch. New regressions pass on Windows and Ubuntu/WSL. The ordinary M3
build/verifier passes at 2,601,280 bytes. Experimental services, installed
firmware and frozen delivery packages remain unchanged.
Full host qualification passes without SKIP; documentation integrity and
`git diff --check` pass.

[PR 46 diagnostics/media review](reviews/2026-10-11-pr46-diagnostics-media-review.md)
covers six further scopes (52 candidate files, 17 service issues), all repaired
locally, plus a reproduced stale service-log acknowledgement boundary and
adjacent SD format failure cleanup. FatFs has one service-unreviewed file and
is not a complete-directory review PASS. New injected regressions pass on
Windows and Ubuntu/WSL; full host qualification passes without SKIP. The final
ordinary M3 build/image verifier passes at
2,601,280 bytes. Installed firmware and frozen packages remain unchanged.

[PR 46 deck/USB/Wi-Fi lifecycle review](reviews/2026-10-11-pr46-deck-lifecycle-review.md)
covers nine further directories (48 files, 13 service findings): twelve findings
have local repairs; acquisition-token lease hardening remains explicitly deferred
after auditing current callers. A manually reproduced Beat Jump overflow is also
repaired. Changes cover LOAD contention, snapshot ordering, network seek bounds,
cue recovery/legacy deletion, settings migration, controller bootstrap/epochs and
USB/Wi-Fi startup races. Full host qualification passes without SKIP; the final
settings refinement has a separate 91-check focused PASS. The final ordinary
M3 build/image verifier passes at 2,600,528 bytes; new USB/Wi-Fi fault harnesses
also pass on Ubuntu/WSL.
Installed firmware and frozen packages remain unchanged.

[PR 46 startup/board/build review](reviews/2026-10-11-pr46-startup-board-build-review.md)
covers six further scopes (51 files, three confirmed service findings), all
repaired locally. Build-time identity refresh prevents stale SHA/dirty metadata;
SD DMA eligibility and the M3 I2S reclock diagnostic boundary are corrected.
Manual repairs support relocated build verification and selected-board startup
reporting. Full host qualification and the ordinary M3 ESP-IDF 6.0.2 build pass,
including actual image/isolation verification; dependency locks are unchanged.
Ubuntu/WSL identity execution remains NOT RUN because Ninja is absent; CI now
declares CMake/Ninja installation explicitly, without claiming a new CI run.
The installed firmware and frozen delivery artifacts remain unchanged.

[PR 46 web/OTA/library/storage review](reviews/2026-10-11-pr46-web-storage-review.md)
covers eight further directories (71 files, 11 findings), all addressed locally.
Repairs preserve HTTP during optional mDNS failure, fence an activated OTA
slot against a second upload, release failed receive resources, distinguish
HTTP server errors, keep cache reads free of mkdir, preserve cache entries on
close failure, support full beat counts, align both tested cache-write policies,
clear failed media IDs and publish fast-reconnect unmount edges. These changes
extend scoped software review; the installed image and frozen packages remain
unchanged. Whole-PR, current CI and physical board gates remain separate.
Full host qualification passes without SKIP, both library cache policies pass,
and the ordinary M3 ESP-IDF 6.0.2 build passes with unchanged locks. The final
storage mount regression passes separately; its runner registration is ready
for subsequent full runs.

[PR 46 profile/USB-host/LED review](reviews/2026-10-11-pr46-profile-host-led-review.md)
covers five further directories (26 files, six findings). Five confirmed
repairs address queue-reset starvation, malformed MIDI output admission,
rescan reactivation, activation logging and C++ LED linkage. The proposed
14-bit pair change remains an explicit compatibility-sensitive deferred item;
it is not reported as fixed. This extends scoped review evidence and does not
change the installed accepted firmware or frozen delivery artifacts.
Full host qualification (no SKIP), ordinary M3 ESP-IDF 6.0.2 build,
documentation checks and focused production-boundary regressions pass.
The existing six compiled controller profiles remain byte-identical.

[PR 46 UI/controller review](reviews/2026-10-11-pr46-ui-controller-review.md)
records nine local UI repairs and one unresolved font-permission/provenance
item from ten service findings. All nine simulator screenshot gates, the
full host qualification and an ordinary M3 build pass; controller-runtime
review reports no findings in seven files. This is scoped review evidence,
not whole-PR approval or acceptance of a new installed image.
The separate 13-file USB-audio review found a confirmed major FS 48-kHz
feedback packet-capacity defect. Its local repair and synthetic 16/24-bit
regressions are recorded in the same review; physical controller formats and
the installed accepted firmware remain unchanged.

[PR 46 scoped audio review](reviews/2026-10-11-pr46-audio-review.md) reports two
confirmed minor CodeRabbit issues repaired locally: FX-ready publication and
media-fault retry spacing. The full service review was rejected at 707 files
against its 150-file limit; audio-core review is partial, not whole-PR approval.
New repair evidence does not qualify the frozen packages or change the installed
Hercules image. Remaining review directories and board gates stay open.

Current review handoff: [P01–P10 dispositions and residual gates](reviews/2026-10-11-shared-core-delivery-readiness.md)
and [prepared PR description](reviews/2026-10-11-shared-core-pr-description.md).
Finding labels now follow the original comparison (P05 beat phase; P06 PDB
title). Software/package rows are closed for frozen `5ff76b6c`; historical
deferred experiments, coverage gaps and operator exclusions
are separately classified. No device action or remote publication is implied.

Operator correction, 2026-10-11: the historical OTA restart was associated with
moving the cable. The current ledger closes it as an operator cable-intervention
event and removes it from open firmware defects. Earlier investigation paragraphs
below remain historical evidence, superseded by this clarification. No firmware
fix, new test or new-image lifecycle acceptance is claimed.

Latest local delivery: three ordinary `5ff76b6c` candidates are signed and
independently verified, with BIN/ELF/MAP, configuration manifests, frozen
qualification evidence and delivery hashes retained locally. The existing
packaging guards ran against clean exact-source HEAD; the working branch was
restored afterwards. No device installation or publication occurred. Exact new
image physical gates remain NOT RUN; hosted CI is SKIPPED BY OPERATOR (quota).
See [frozen-source signed delivery](validation/SHARED_P4_LOCAL_QUALIFICATION_20261011.md#frozen-source-local-signed-delivery-2026-10-11).

Latest Hercules shortened gates on unchanged clean `e16ecfb5`, VALID `ota_0`:
three stopped USB reconnect/recovery observations with operator Browse/LED
confirmation, one separate cold-power readiness/GUI check, 60.219-s mixed-rate
dual MT/loop audio/visual PASS and 20.656-s active jog/Browse PASS (2044 MIDI
packets). All 22 playback strict deltas/output-late zero; decks stopped.
The combined lifecycle recorder remains INCOMPLETE; independent sample audit
establishes three USB epochs, with middle-epoch MIDI growth unsampled.
Final soak and wider control coverage remain open. See
[shortened gates](validation/HERCULES_INPULSE_500_PREPARATION_20261010.md#shortened-lifecycle-and-mixed-rate-mt-gates-2026-10-11).

Latest Hercules wake/protection gates on unchanged clean `e16ecfb5`, VALID
`ota_0`: first PLAY D1 wakes screensaver without playback; subsequent PLAY
starts normally. Physical normal/Shift LOAD on each playing deck and web LOAD
are refused, with unchanged tracks and operator-confirmed clean audio.
The dual-playing window completed 90.485 s, all 22 strict deltas and output-late
delta zero, final decks stopped. Full-sample audits pass. Cable reconnect/cold
power, wider controls, final mixed-rate MT smoke and soak remain open.
Evidence: [Hercules wake and LOAD LOCK](validation/HERCULES_INPULSE_500_PREPARATION_20261010.md#hercules-screensaver-wake-and-load-lock-2026-10-11).

Latest pad/FX/LED observation on `e16ecfb5`: 180.281 s / 313 samples /
136 MIDI packets, all 22 strict deltas and output-late delta zero, both decks
finally stopped. Sampled loop changes are incomplete relative to the requested
sequence and no global FX change was observed. Operator reports
"sve je bilo uredno" for the listed controls/audio/visual question; no
individual completion list was supplied. This is not full pad/FX/LED
qualification. See the
[combined observation](validation/HERCULES_INPULSE_500_PREPARATION_20261010.md#combined-padfxled-observation-2026-10-11).

Latest installed manual-loop candidate: clean `e16ecfb5`,
`M3-dev-ge16ecfb5e7b0`, VALID `ota_0`, exact ELF
`96279a267ef9a93f89148ba01de95519c93a87d6581f609e576739002323252f`.
Complete host qualification without SKIP, M3 ESP-IDF 6.0.2 build, identity/budget,
restart link placement and signed packaging pass. One local OTA returned HTTP
200; the exact image reached VALID and both decks remained stopped. Hercules
MIDI/UAC1 44100-Hz stream is healthy. Operator confirmed GUI restoration and
readiness for the targeted manual-loop retest. That 90.406-s / 164-sample test
passes all 22 strict deltas and output-late delta zero. Both physical IN/OUT/Exit
routes and decoder prefix publications are observed, with both decks stopped.
Operator confirmed normal loop/Exit response, clean MAIN/Hercules headphones
and fluid waveforms. The observed manual-OUT PCM regression is closed for this
exact tested configuration; other gates stay open.
No JC build, push, release or public OTA was performed.

Previous installed restart/Hercules-support candidate: clean `ac9287af`,
`M3-dev-gac9287af4a4c`, VALID `ota_1`, API idle, both decks stopped. Hercules
`06F8:B12B` MIDI/profile and UAC1 full-speed packed 24-bit/44100-Hz stream
are active; decks are stopped. Explicit feedback updates grow without invalid values. The missing
HCD integration was its 512-byte HS periodic FIFO, below Hercules MPS 540;
the experiment now uses 640 bytes while standard layouts remain unchanged.
Full host qualification of the claim diagnostic passed without SKIP; the final
FIFO patch additionally passes four compiled policy configurations. The exact
final M3 ESP-IDF 6.0.2 build, board identity/budget, restart link/raw gates,
signed bundle/manifest and local OTA pass. Matching BIN/ELF and hashed evidence
are retained. JC builds and public release were not run.

The cache synchronization restart candidate passes the complete qualification
host suite without SKIP and its exact M3 ESP-IDF 6.0.2 build, linked IRAM/panic
bypass contracts, five raw fixtures, board/budget checks and signed packaging.
One authorized local OTA installed it. A subsequent stopped normal restart
passes boot 418 -> 419, reset `SW`, the same exact VALID image and byte-identical
crash dump, with Hercules MIDI/UAC1 44100 Hz restored. The peer-reset/cache-sync
serialization gap is addressed; causality and broader restart reliability remain
open. The operator confirmed that the new-image GUI is normal on 2026-10-11.
This confirms GUI restoration, not new-image playback/audio qualification. Evidence and scope:
[restart candidate](validation/RESTART_BUS_CAPTURE_20261010.md#cache-synchronization-candidate-after-the-natural-fault).

An unattended read-only unloaded idle observation on exact `ac9287af` completed
180.172 s / 171 audited samples: all 21 connection/loss counter deltas zero,
45,121 new valid feedback updates, no USB disconnect or reboot, output-late
delta zero. Hercules remained UAC1 FS/44100 Hz and both decks remained unloaded
and stopped. Empty-ring zero-fill underflow grew by 7,960,226 frames with zero
submitted producer frames; it is explicitly separate in this idle-only scope,
not a zero-underflow claim or relaxation of the 22-counter playback gate.
Deterministic local profile regeneration matches the existing SHA. Current
controller README/MIDI acceptance state is reconciled, with combined remaining
operator blocks prepared. No new build/OTA/playback or S2 retest was performed.
Evidence and limits: [unattended Hercules observation](validation/HERCULES_INPULSE_500_PREPARATION_20261010.md#unattended-unloaded-idle-observation-2026-10-11).

On the same `ac9287af`, the returned operator completed the stopped navigation
and mixer window (180.234 s / 328 samples / 11,574 MIDI packets) and its focused
Gain/Shift+LOAD supplement (60.469 s / 110 samples / 3304 MIDI packets).
Normal and shifted load routes preserve the other deck; both Gain, pitch,
faders, EQ/filter and crossfader movements are observed, with operator-normal
response. No reboot or guarded connection/ISO errors occurred; both decks stayed
stopped. This is partial controller coverage, not playback, analog calibration,
pad/LED or full transport acceptance. Details:
[stopped controls](validation/HERCULES_INPULSE_500_PREPARATION_20261010.md#stopped-navigation-and-mixer-blocks-2026-10-11).

The subsequent manual transport window is strict PCM2 FAIL at 110.719 s / 199
samples: PCM2 +795 immediately after the first observed D2 IN/OUT loop,
other 21 strict deltas and output-late delta zero, no new USB loss or reboot.
Both decks stopped; operator noticed no symptom and identified D2 loop controls.
The decoder's FLAC-only retained-IN prefix left indexed MP3 with only a 2048-frame
reseek reserve. A bounded correction extends prefix eligibility to verified-index
MP3; real decoder/prefix continuation and slow-seek host regressions pass.
The complete host qualification suite and the exact M3 build/package pass;
the new `e16ecfb5` is installed VALID. Targeted manual-loop telemetry passes;
operator audio/visual confirmation also passes. Previously checked CUE/PLAY
handoff and Sync are retained per operator clarification; no repeat scheduled.
Evidence and limits: [manual loop failure](validation/HERCULES_INPULSE_500_PREPARATION_20261010.md#manual-loop-transport-failure-and-bounded-mp3-correction-2026-10-11).

On earlier exact `04f0cfb6`, the operator-confirmed combined 60.078-s test passes MAIN/headphones, both
jogs/Browse, PFL D1/D2, native headphone Mix/Volume and fluid waveforms. All 85
samples were audited: all 22 strict deltas zero, 649 MIDI packets, output-late
delta zero. This is a short smoke, not full Hercules qualification. Profile is
5592 bytes, v4; other stored profiles remain unchanged. Details and remaining
gates: [Hercules record](validation/HERCULES_INPULSE_500_PREPARATION_20261010.md).

The outgoing `90fba690` OTA restart produced a natural panic even though the
new image reached VALID. The 7776-byte raw dump and captured core-0 register
sequence decode against its exact retained ELF into `Cache_WriteBack_All`.
Capture/export is proven; the reason for the bus fault and restart reliability
remain open. The original historical dump is preserved separately. See
[natural restart capture](validation/RESTART_BUS_CAPTURE_20261010.md#first-natural-captured-ota-restart-fault).
S2 testing stays operator-closed; no push, release or public OTA occurred.

[Restart bus capture implementation](validation/RESTART_BUS_CAPTURE_20261010.md)
adds bounded shared IRAM hooks and 256 bytes of internal RTC user-coredump memory
to retain frame/queue state before IDF PC normalization. Focused production
wrapper and decoder tests pass. Actual link/coredump-region and synthetic raw
pipeline checks are now mandatory build gates. Full qualification host suite
passes with no SKIP. Clean M3 `0b08fb90` passes ESP-IDF 6.0.2, linked RTC-region
and interception checks, five raw-pipeline fixtures, board identity/isolation
and signed development packaging. Its matching BIN/ELF and verification hashes
are retained locally. A signed local OTA installed clean `0b08fb90`, VALID
`ota_1`, API idle; the operator confirmed GUI restoration. Both decks were
stopped and no controller/audio stream was active. The historical 7456-byte
crash export is byte-identical and preserved. At that earlier checkpoint,
physical PANIC export and restart reliability were **NOT RUN**; that restart does not
qualify the earlier crash workload or reopen S2 testing.

[Offline historical restart audit](reviews/2026-10-10-m3-restart-crash-analysis.md)
confirms core 0, an internal interrupted stack and the cache-writeback call
chain by complete original-BIN function-byte checks. One isolated historical
M3 analysis build passes; its ELF differs from the original and is used only
for individually verified functions. The original panic handler can replace
PC from an asynchronous bus-error queue. Missing queue CSRs/pre-normalization
MEPC prevent identification of the rejected transaction and causal repair.
The latest successful OTA had no controller or USB audio stream, so it does
not qualify the earlier workload. Device state and S2 closeout remain unchanged.

[Crash diagnostic implementation](validation/CRASH_DIAGNOSTICS_20261010.md)
adds distinct stored-dump ELF identity, a bounded marked stopped-deck raw export
and verified local ELF/map archives. Focused behavioral regressions pass;
full host suite, the single M3 IDF 6.0.2 build, board/budget checks and actual
signed packaging/debug archive verification pass on clean `58f09941`.
Documentation checks pass. After the operator clarified USB1 is power-only
and authorized the dump-overwrite risk, one signed local OTA installed clean
`58f09941`, VALID `ota_0`, boot 412 with reset `SW`. The new endpoint exported
the complete 7456-byte historical dump; its embedded checksum passes offline.
The stored nine-character ELF prefix `5f70083d0` matches the recorded outgoing
`3f3605a7` identity prefix, not a full SHA-256 match. Registers/segments and
original bytes are retained with hashes. The original application ELF remains
missing, and the historical restart PANIC cause remains open. S2 testing stays
closed with waivers; its accepted `a9f66bcd` ELF/map and exact-image soaks remain
historical evidence, not acceptance of the installed diagnostic image.

Current S2 disposition, 2026-10-10: **work closed with explicit operator
waivers; no further S2 testing**. Exact clean `a9f66bcd`, VALID `ota_1`, retains
full-hour 48000/44100-Hz telemetry and physical playback PASS and 48000-Hz
active jog/Browse acceptance. Remaining 44100-Hz active controls, exhaustive
LED/held-state and outstanding physical lifecycle/profile qualification are
WAIVED BY OPERATOR / NOT RUN, never converted to PASS. Earlier pending entries
below remain historical. No unrestricted S2/shared-core production release.

The [closeout/resource/restart review](reviews/2026-10-10-s2-closeout-resources-restart.md)
records the continuing general investigation: all 5260 cached soak samples show
no progressive retained-memory decline, a stopped snapshot has zero allocation
failures and adequate recorded task stack reserves, and largest-block DMA
qualification remains unavailable. Original outgoing-BIN disassembly supports
the cache-writeback restart location; its cause and raw-dump/ELF association
were unresolved at that review checkpoint. The diagnostic capture above adds
raw bytes and registers, but no causal repair or new S2 qualification. No new
build, S2 test, push, release or public OTA was performed during this capture.

The [Hercules preparation record](validation/HERCULES_INPULSE_500_PREPARATION_20261010.md)
documents PC VID/PID and jog-touch capture, exact fixture regeneration and
profile/registry host checks. A new shared v4 native-headphone capability and
independent filter policy resolve the software output-control audit; focused
production PCM/parser/storage/runtime tests pass. Full qualification and the
new M3 firmware build/install now pass on `eafdb7a5`. Actual descriptors, native blend,
controls/LEDs and audio require physical Hercules acceptance on M3.
Hercules M3 audio/MIDI/LED/lifecycle acceptance remains NOT RUN. This offline
work does not change the concurrent S2 exact-image soak.

The 2026-10-08 [S2 performance candidate](../controllers/ni_traktor_s2_mk3_full/README.md)
documents source-backed mixer/pitch/jog/LED coverage, raw-10 inversion and
experimental HS UAC2. The [S2 evidence record](validation/S2_MK3_PROFILE_PREPARATION_20261007.md)
separates prior basic-profile acceptance from the new combined physical checks.
On clean installed `0729221a`, stopped-deck reconnect and Wi-Fi OFF/ON,
60-second dual MT/loop audio/visual playback and a 20-second active jog/Browse
supplement passed with operator confirmation and zero new strict audio/USB
counter deltas. These checks used the diagnostic 2-dBm Wi-Fi limit and retain
that image/configuration scope.

The prior clean candidate `ac864bdc` was installed VALID in `ota_0`; boot 401
records the normal 20-dBm Wi-Fi maximum. Its full host/M3 build and signed
package checks pass. Three normal-power loaded-track stopped reconnects,
60-second MT/loop audio/visual playback and a 30-second active jog/Browse
supplement passed with operator confirmation and zero new strict playback
counter deltas. Normal-power stopped-deck Wi-Fi OFF/ON also passed on boot 401
with preserved MIDI/audio readiness and repeated 20-dBm readback. Full
controller/reliability coverage and the final exact-image soak remain open.
The attempted 3600-second mixed-rate/MT/loop S2 soak is **FAIL**: one ISO
packet / six frames were lost at elapsed 306.865 seconds (23:10:59 local).
The operator noticed no audio, display or controller disturbance; the strict
loss criterion still fails. Both decks were safely stopped at the checkpoint;
the last recorded elapsed time was 1056.550 seconds. The observer had recorded
the failure without exiting its loop, so no native final result exists.
Preserved evidence and the checkpoint annotation are linked in the S2 record.
The host monitor now exits TimingSoak immediately on a hard failure and records
actual elapsed time. The heartbeat is paused. One isolated 11297-us output-late
event remains a separate monitoring finding. ISO root cause and exact-image
requalification remain open; no replacement soak was started at that checkpoint.

The subsequent ISO-rearm investigation confirms a same-microframe hardware
activation preceding the skipped packet. Mitigation candidate `00a1ca90`
reduces diagnostic task-context cache chunks to 1 KiB. Full host/M3 build,
board identity and signed package checks pass; it is installed VALID in
`ota_1`. The 60-second mixed-rate MT/loop retest passed with operator-confirmed
clean MAIN/S2 headphones and fluid waveforms, zero strict deltas and zero new
output-late events. The separate 600-second same-workload test failed after
514.981 seconds on two skipped ISO packets / 11 lost frames. Both decks were
automatically stopped; the heartbeat is paused. The 1-KiB bound did not
eliminate the fault. The final 60-minute soak remains NOT RUN.
The failed `ac864bdc` soak is retained; it does not qualify this candidate.

The subsequent `17f75286` candidate raises only M3 HS-diagnostic USB IRQ
priority and improves bounded packet/activation context. Full host/M3 build,
identity and signed packaging pass; it is installed VALID in `ota_0`.
Boot 403 reports PANIC after the old image's `ota_reboot` cache writeback path;
MIDI is present but the intended audio quarantine prevents preparation.
Operator reset restored boot 404 POWERON, healthy S2 44100-Hz streaming and
normal Wi-Fi. After operator MT/zoom/readiness confirmation, the 600-second
IRQ retest started around 00:14:36 Europe/Zagreb on 2026-10-10 and failed after
90.586 seconds on two skipped ISO packets / 11 lost frames. Cleanup stopped
both decks and the heartbeat is paused. Packets 1/2 of the same 56-packet block
were skipped without a captured late activation. The next candidate isolates
ISO descriptor lists in a fixed 2-KiB internal pool. Clean candidate `3f3605a7`
passes full host/M3 build, identity and signed packaging and is installed VALID
in `ota_1`. Boot 405 is a normal SW reset, with healthy MIDI/audio and Wi-Fi.
The descriptor pool is confirmed in internal BSS. Tracks are prepared/stopped;
operator reconnect/MT/zoom setup and physical retest are pending.
No playback/IRQ physical PASS is asserted;
the restart failure and sustained ISO-loss gate remain open.

## Shared-core migration authority

Canonical source is `dvucinozd/Pajoniiir`, branch `codex/shared-p4-core-m3`.
[Integration/provenance](SHARED_P4_CORE_INTEGRATION.md),
[board/capability policies](SHARED_P4_BOARDS.md),
[preserving wired migration](M3_SHARED_CORE_MIGRATION.md),
[explicit cue migration](M3_CUE_MIGRATION.md) and
[exact-image monitoring](RELIABILITY_MONITORING_PLAN.md) govern this candidate.
Earlier JC and M3 release/acceptance records retain their original image scope.
Build, package, installed-device, operator acceptance and publication evidence
must be recorded separately. Current shared-M3 fb5cbca1 has exact-image
waveform/60-minute Campaign B acceptance and completed
[negative OTA/injected startup rollback gates](validation/M3_OTA_NEGATIVE_ROLLBACK_20261007.md).
Physical Campaign A and current-source GitHub Actions are SKIPPED BY OPERATOR
(Actions quota); remaining hardware/integration gates remain open.
The M3 repository remains historical and has not been remotely archived.

## Published product

The current public release is **M2.5**, frozen at
`20f1c3f04a615209ae25e9bbdae649d0f5b44e8d`. It is accepted for
**JC4880 / DDJ-FLX4 / PCM5102A MAIN / FLX4 headphone cue**, with the previous
product design and regular configuration. Preview, recorder and SD experiments
are disabled. The [release record](validation/M2_5_RELEASE_20261006.md)
is authoritative for artifacts, physical acceptance and publication.

| Item | Frozen release value |
| --- | --- |
| Release / annotated tag | `M2.5` |
| Source | `20f1c3f04a615209ae25e9bbdae649d0f5b44e8d` |
| Project / toolchain | `main-deck-p4` / ESP-IDF v6.0.2 |
| Application | 2,576,432 bytes; SHA-256 `7aaab31d4a7c00990939cb9ccecb04aba1cba463b1db919d168477c7df3210ba` |
| Signed bundle | 2,576,620 bytes; SHA-256 `44db191c1785168781e9b2893cac6dda1b92a20a5342c328d7fbfdbbd8707e27` |
| Public channel | [latest.json](https://ota.pajoniiir.eu/latest.json) |
| GitHub release | [M2.5](https://github.com/dvucinozd/Pajoniiir/releases/tag/M2.5) |
| Last publication-session device observation | M2.5 / `ota_1`, boot 618; decks stopped, AP/API and USB/FLX4 available, startup ready, zero allocation failures |

A recorded slot/boot is session evidence, not a fresh observation of the device.
Documentation and later diagnostic commits on `master` do not alter the frozen
image. The integration branch was merged into `master` on 2026-10-05; it is
no longer a separate source of unreleased A-L work.

## Acceptance and limits

Final M2.5 passed signed OTA, startup readiness, USB/FLX4 availability and
62.622211 seconds of dual playback with zero new strict audio/USB faults and
operator-confirmed Overview/MAIN/D1/D2 cue. The operator explicitly waived
a new 180-minute M2.5 soak because audio/deck/DSP code is unchanged. This is
an image-specific exception; the following v91 evidence remains historical.

Focused exact-v91 scenarios passed with operator-confirmed sound:
MP3/WAV/FLAC (including 96 kHz/24-bit FLAC), MAIN/D1/D2 cue, scratch,
CUE/PLAY/Master Tempo, manual loop OUT/half/double/exit, combined scratch/MT,
local cue save/reload/reboot/delete, normal D1 LOAD lock, stopped USB storage
and FLX4 reconnect, and post-reconnect playback.

The saved soak intervals total **10,801.3664176 seconds (180m01.37s)**:
1,015.8899243 seconds followed, after interruption, by 9,785.4764933 seconds.
The completed continuation has zero strict audio/USB fault deltas, valid
memory/stack reserves and operator-confirmed clean sound. This is an explicit
operator-accepted **segmented-soak release exception**.
**An uninterrupted 180-minute v91 soak remains NOT RUN.** Neither historical
monitor failure is erased or converted into PASS.

Diagnostic-only images separately passed unconfirmed-image rollback and
startup-readiness rejection at **60.253 seconds**, automatically restoring v91.
The latter forces the readiness input false while real AP/API remain available;
it does not prove an induced radio failure. Diagnostic images are excluded
from ordinary release packaging.

These scenarios remain NOT RUN on the frozen release:

- other deck/format combinations and full advanced transport variants;
- shifted/touch LOAD-lock variants;
- source-cue tombstone reimport and physical cross-media cue isolation;
- exhaustive playlist/artwork/PWV4, touch and metadata comparison;
- active removal/cancellation and unexercised held-control reconnect variants;
- JC1060, DDJ-400, other controllers, real CDJ/rekordbox Link peers, output
  phase/latency calibration and experimental SD/recorder qualification.

Unrun scenarios remain outside the accepted scope; software coverage does not
turn them into physical passes. Full details and failed intermediate candidates
are retained in the [hardware ledger](validation/JC4880_L_CANDIDATE_20261005.md)
and [release review](validation/JC4880_V91_RELEASE_REVIEW_20261005.md).
Automatic recovery passed in the accepted configuration. The earlier bare
M2.4 power-cycle observation is historical, not a universal v91 requirement.

A [focused library smoke](validation/JC4880_LIBRARY_SMOKE_20261006.md) on
installed v91 additionally confirmed artwork and memory-cue visibility,
navigation of `untitled playlist`, catalog consistency, local load and rejection
of stale catalog requests. Exported order, cue positions, downbeat/PWV4 source
comparison and artwork isolation remain open; the development logo was not
installed during these checks.

## Maintained architecture and software scope

Released M2.5 additionally includes the user-supplied default Pajoniiir
artwork logo. Overview and track Library rows use pre-rendered read-only
thumbnails when real artwork is unavailable; empty product decks and folder
rows remain distinct. It is absent from the immutable v91 image. Before final
M2.5, a clean `M2.4-111-ge09e9ca4` candidate from `e09e9ca4` was
installed in `ota_0` through signed OTA. Startup, API, FLX4 and storage
are available, with zero allocation failures, and the operator confirms logo
visibility in Library and on loaded D1 (`Red For Love {320}`). A matched
stopped-track memory comparison has unchanged largest internal/DMA blocks
and only eight bytes less free memory, matching BSS growth. A subsequent
62.419-second dual-deck audio check passed with operator-confirmed MAIN/D1/D2
cue sound, zero strict fault deltas and zero allocation failures. Largest
internal/DMA blocks remain 31,744 bytes during playback. The empty-deck
startup largest-block variation is retained in the record; this focused check
does not constitute a new exact-image 180-minute soak. The operator also
confirms correct real-cover-to-logo replacement and two-page Library browsing
without stale artwork. Media replacement and delayed/corrupt cover variants
remain open. See the
[candidate record](validation/M2_5_DEFAULT_ARTWORK_20261006.md).

One authoritative P4 core serves two board entrypoints:

| Configuration | Identity | Status |
| --- | --- | --- |
| JC4880 / FLX4 / PCM5102A MAIN + USB cue | `firmware/main-deck-p4` / `main-deck-p4` | Published frozen M2.5; focused physical scope and explicit soak waiver above |
| JC1060 / DDJ-400 / USB MAIN + cue / Ethernet Link | `firmware/main-deck-jc1060` / `main-deck-jc1060` | Software verified; all new-board/peer physical gates NOT RUN |

Packages A-L are software verified. The [integration ledger](FORK_IMPROVEMENTS.md)
maps modules and dated evidence, including adaptations from collaborator
[kayrozen](https://github.com/kayrozen), donor
`428b97dd4a175f03d3a172c8db9c4d5ed94195fb`. Persistent media identity,
audio-session fencing, worker ownership and signed OTA remain common-core
contracts. S3CP v2/v3/v4 parsing, compilation, storage and upload validation are
integrated; public Web Profile Builder deployment is a separate project gate.

JC4880 keeps USB0 storage, USB1 FLX4 and Wi-Fi service; it has no Link provider.
JC1060 Link is default OFF and Ethernet-only, with bounded discovery/claim,
browse, verified SD downloads and network sync. Unidentified NFS volumes use
session-local identity and fresh downloads, so local edits cannot migrate
between those sessions. LOCKED is a model state; sink/rate latency is
UNMEASURED until physically calibrated and engineering calibration is RAM-only.
No local browsable Link server is advertised.

The P4 owns playback, deck position, DSP, controller binding, LEDs and UI.
There is no active S3 firmware, UART control link or inter-board PCM bridge.
Compatibility names `control_link`, `S3CP` and `profile.s3bin` do not imply S3.
APTA remains on its separate branch and is outside this release.

## Security and inherited evidence

Shared service credentials, unchanged enclosed wiring and uncaptured numeric
thermal/RF/strain margins are accepted limitations. Secure Boot, Flash
Encryption and security eFuses remain disabled. Offline encrypted primary
and backup signing-key copies were operator-confirmed; backup recovery signing
and successor-key overlap remain future maintenance. See
[security policy](SECURITY_PROVISIONING_POLICY.md) and [risk register](RISK_REGISTER.md).

Earlier releases and validation records preserve only their own image results.
Start from the [documentation index](README.md) for the M2.1-M2.4 history,
duplicate-track-ID candidate acceptance, power/VBUS and lifecycle records.
Do not transfer historical passes to new code or claim waived tests were run.

## Source-of-truth order

For the current S2 strict ISO investigation, see the
[2026-10-10 scheduling and evidence analysis](reviews/2026-10-10-s2-hs-isoc-loss-analysis.md).
It identifies observability gaps and the next causal capture; it adds no
firmware or physical acceptance claim.

The subsequent bounded first-fault observer is implemented and installed as
clean `a9f66bcd`, VALID `ota_1`. Full host suite, M3 ESP-IDF 6.0.2 build,
board/image and signed-package checks pass. The API confirms capture enabled,
not yet captured; S2 MIDI and live 48000-Hz audio are active, decks prepared
and stopped. This diagnostic image has no new physical playback/soak PASS.
See the [S2 validation record](validation/S2_MK3_PROFILE_PREPARATION_20261007.md)
for exact SHA, ELF, artifact hash and outstanding reproduction gate.

The observer's short playback completed 60.514 s / 95 samples at S2 48000 Hz,
all 22 strict deltas and output-late delta zero, both decks stopped. Capture
is enabled but empty. Final diagnostic-log text was recovered after a helper
JSON-decoding error; original evidence is preserved. Physical confirmation
and long fault reproduction remain pending.

The operator subsequently confirmed that short observer playback had clean
MAIN/S2 audio and fluid waveforms. A bounded independent first-fault capture
run started on the same image at approximately 17:40:04 local, 2026-10-10,
up to 3600 seconds or the first strict error. Initial fresh status confirms
dual playback, S2 48000 Hz and zero packet failures/output-late. Fifteen-minute
checkpoints are active; final diagnostic outcome and long acceptance are open.

That first-fault diagnostic window completed 3600.296 s / 5705 samples on
exact clean `a9f66bcd`, S2 48000 Hz: all 22 strict deltas and output-late delta
zero, both decks stopped, same ELF/VALID `ota_1`. All samples were audited;
capture is enabled but empty. ISO loss was not reproduced, so no causal fix
is established and the 44100-Hz path remains unqualified. The operator
confirmed clean MAIN/S2 audio, fluid waveforms and stable display for the full
hour: exact-image 48000-Hz playback telemetry and physical PASS. Active-control
qualification remains separate. Minimum observed DMA free is 8759 bytes; lifecycle
and headroom remain open. Final evidence and hashes retained, one-time task
removed after runner exit, heartbeat paused. No push or publication.

The [unattended post-soak review](reviews/2026-10-10-s2-post-soak-headroom-lifecycle.md)
checks all recorded memory samples and reconnect/restart code. Cached free
internal/DMA memory has no observed progressive decline, but largest blocks
and lifecycle reserves remain unqualified. The extended production-stream
host regression passes 30 reconnect cycles with bounded live reservations,
no new OUT allocations and full release after FS transition. These are host
tests, not physical Campaign A. The prior OTA crash address matches the cache
sync map register; its underlying failure is still unresolved. No new firmware
build or install is needed for these test/documentation changes.

Full P4 host suite for the unattended test changes passes with exit code zero,
without SKIP markers. The existing a9f66bcd signed package/manifest verifies.
Physical active-control/reconnect and reboot checks were NOT RUN while the
operator was away; the installed exact-image playback PASS is preserved.

After operator return, a 30.042-second active-control block on the accepted
a9f66bcd/S2 48000-Hz setup passed: 1122 MIDI packets, all 22 strict deltas and
output-late delta zero, decks stopped. Operator confirmed both jogs/Browse,
MAIN/S2 audio and waveforms were correct. Short active-control telemetry and
physical PASS; reconnect/lifecycle and the intermittent ISO cause remain open.

The first physical reconnect capture stopped during its first removal and the
operator subsequently identified an incorrectly seated cable. Original fault
evidence is retained; this is operator-invalid, not a clean three-cycle PASS.
Post-operator counters show three reconnections and healthy final S2 44100-Hz
audio, decks/tracks and storage preserved. Per-cycle telemetry remains open;
the 48000-Hz playback PASS is not transferred to 44100 Hz.

The repeated reconnect observer captured three operational MIDI/audio returns
at 44100 Hz; operator confirmed both Browse controls. Tracks, stopped decks,
storage and image remained unchanged, allocation/root-recovery failure deltas
zero. One ISO packet/five frames was lost before the first removal; operator
explicitly confirmed manipulating cable or power in that period. Evidence is
preserved with context and hashes; neither spontaneous firmware causation nor
manipulation as the cause is established. This is not clean ISO qualification.
The completed task was removed after exit. Shared monitor ISO checks now also
cover stopped Observe/UsbRecovery; the regression reproduced false PASS before
the fix and all 11 monitor tests pass after it. No firmware build or OTA change.

A separate read-only 600-s idle observation of S2 44100-Hz streaming started
2026-10-10 19:47:11 local with both decks stopped and no intentional USB/power
changes. Records `stopped-idle-20261010T174710Z` under the next-gates directory;
independent task and live process verified. Baseline ISO 1 packet/5 frames and
frozen fault remain retained. Result is pending; this does not qualify playback
or full S2 acceptance. Checkpoint automation will report completion and pause.

That stopped S2 44100-Hz idle observation completed 600.019 s / 1084 samples,
wrapper exit zero. All samples preserve exact clean `a9f66bcd`, stopped decks,
tracks, storage and healthy live streaming; all 22 strict deltas and output-late
delta zero. Final firmware remains VALID `ota_1` with the same ELF. Existing
ISO 1/5 counters and frozen capture are unchanged. Ten-minute idle telemetry
PASS only; 44100-Hz playback, full S2 acceptance and ISO causation remain open.
Full-sample audit/hashes saved, task removed after exit, automation paused.
No firmware, playback, connection, reboot or publication changes.

A separate 600-s S2 44100-Hz dual MT/loop playback window started at about
2026-10-10 20:07:52 local after operator restored zoom 8 and confirmed readiness.
Exact `a9f66bcd` remains installed; one independent monitor checks all22strict
deltas and safely stops both decks on error/end. Initial playing samples show
unchanged ISO counters (historical1packet/5frames). Prior frozen fault is not
new playback evidence. Records `playback-441-20261010T180751Z` under next-gates;
task/runner verified, checkpoint active. Outcome and physical confirmation
remain pending; no new firmware/build/USB/Wi-Fi changes or publication.

That S2 44100-Hz ten-minute playback window completed 600.622 s / 877 samples,
all 22 strict deltas zero, safely stopped, same clean VALID `a9f66bcd`/ELF.
Full-sample audit confirms dual MT/loops, mixed-rate source tracks, opposite
pitch and healthy streaming. Three output-late events (max11334us, largest
sample-observed 60-s cluster2) remain monitoring findings. Operator confirmed
clean MAIN/S2 audio, fluid dual waveforms and stable display for the whole run:
exact-image ten-minute 44100-Hz playback telemetry and physical PASS. MIDI0
does not qualify active controls; existing ISO1/5 and frozen capture unchanged.
Long 44100-Hz qualification and intermittent ISO cause remain open. Evidence,
hashes/operator confirmation preserved; task removed after exit, automation
paused. No firmware, reboot, USB, push or publication changes.

Final 3600-s S2 44100-Hz timing soak started about 2026-10-10 20:28:00 local
on the same clean VALID `a9f66bcd`, dual MT/loops, mixed-rate tracks, opposite
pitch and zoom8. One independent task/runner is verified; initial playing
samples retain historical ISO1/5, feedback-invalid3 and output-late3. Records
`final-soak-441-20261010T182759Z` under next-gates; checkpoints every15minutes.
Strict losses stop the run; output-late rolling cluster/max findings are
recorded separately rather than treating total10 over an hour as a cluster.
Four local offline policy regressions pass. Outcome/operator acceptance pending;
old frozen capture remains historical. No firmware/build, USB or Wi-Fi change,
push or publication; full S2/shared-core and ISO causation remain open.

The final S2 44100-Hz soak completed 3600.414 s / 5260 samples, wrapper exit0.
Full-sample audit confirms exact clean `a9f66bcd`, unchanged dual MT/loop/pitch
workload and healthy streaming; all22strict deltas0, both decks safely stopped,
same ELF/VALIDota1 and zoom8. Five output-late events, max11348us/60-s cluster2,
remain monitoring findings; no investigation trigger fired. Cached DMA free
starts/ends11163B, sampled minimum8635B; prior lifetime minimum7371B unchanged.
Historical ISO1/5 and frozen capture unchanged, MIDI0. Exact-image full-hour
44100-Hz telemetry and physical playback PASS: operator confirmed clean
MAIN/S2 audio, fluid dual waveforms and stable display throughout the hour.
Root cause and remaining S2/shared-core gates stay open. Original hashes
rechecked, operator confirmation and updated audit preserved,
task removed after exit, automation paused; no firmware, restart or publication.

The [2026-10-11 shared-core/Hercules scope review](reviews/2026-10-11-shared-core-hercules-closeout.md)
reconciles original phases and P01–P10 with current source and acceptance.
The unchanged Hercules fixture regenerates identically; focused profile,
version/DMA, fixture and both migration regressions pass. All three entrypoints
select one canonical set of reviewed modules and board providers. The profile
support table now separates implemented pads 1–4 from unsupported FX pads 5–8
and deferred functions. Final current local full qualification/build matrix,
real-media/JC physical gates and historical restart causation remain open;
hosted CI remains SKIPPED BY OPERATOR. This review does not flash or publish.

[Ordinary local software qualification](validation/SHARED_P4_LOCAL_QUALIFICATION_20261011.md)
subsequently passes on clean `5ff76b6c`: complete no-SKIP host suite, all nine
unchanged simulator baselines, JC/M3 300-s keylock policies and fresh IDF 6.0.2
builds JC4880 2605344 B / JC1060 2663424 B / M3 2598608 B. Board/component/source
identity, unchanged locks, budgets and 15 BIN/ELF/MAP/identity/budget artifact
hashes pass. The simulator CMake omission of shared `ui_idle.c` was corrected;
production firmware and installed e16ecfb5 remain unchanged. Fresh experiment
firmware matrix, physical gates, signed delivery and publication stay separate.

Final Hercules exact-image `e16ecfb5` 44100-Hz playback soak completed
3600.016 s / 5171 samples, wrapper exit 0. Every sample and final stopped
snapshot have all 22 strict counter deltas zero; clean source/ELF/VALID ota_0,
mixed-rate MT/loop/pitch workload and zoom 8 verified. Operator confirmed clean
MAIN/Hercules headphones, fluid dual waveforms and stable display throughout
the hour. Fifteen output-late events, max 12534 us and rolling-60-s cluster 2,
remain monitoring findings; no investigation trigger fired. This is telemetry
plus physical PASS for the named final playback scope, not zero-late or full
profile/shared-core completion. MIDI delta 0; the earlier 2044-packet active-input
supplement remains separate. Audit, original artifacts and hashes retained;
task removed after exit and heartbeat paused. S2 remains operator-closed.
See [Hercules validation](validation/HERCULES_INPULSE_500_PREPARATION_20261010.md).

1. Exact release artifacts/configuration and current code/tests for their own SHA.
2. [Published M2.5 record](validation/M2_5_RELEASE_20261006.md) for accepted
   hardware and publication; this status for current scope.
3. Operational architecture, wiring, OTA, security, checklist and risk documents.
4. Dated validation and integration history for their named images.
5. Git history for retired implementations and superseded plans.

The [documentation audit](validation/DOCUMENTATION_RECONCILIATION_20261006.md)
records this reconciliation. A documentation-only update does not reflash,
requalify or republish firmware.

The 3f3605a7 internal ISO pool ten-minute test has telemetry and operator-confirmed playback PASS: 600.408 s, 983 samples, all 23 strict deltas zero, zero new output-late events, both decks stopped. MAIN/S2 headphones and dual waveforms were physically confirmed clean/fluid throughout. S2 ran at 48000 Hz; prior failed candidates ran at 44100 Hz, so this does not establish a sole root cause or qualify the 44100-Hz path. Controls were untouched (MIDI delta zero). Final 60-minute soak, active-control and remaining lifecycle gates remain open; checkpoint automation is paused.

Final exact-image 3f3605a7 S2 48-kHz soak is INCOMPLETE: host observer absent without result.json at the 01:13 checkpoint; records stopped at 1032.793 s (00:59 local). Last recorded and live audio loss/output-late deltas were zero. Both decks were safely stopped, evidence retained, and heartbeat paused. Cause of observer disappearance is unproven; this is not a completed 60-minute PASS. Earlier ten-minute PASS retains its scope.

The independent final 3f3605a7 S2 48-kHz soak is strict ISO FAIL at 1336.174 s / 2185 samples: two packets and 12 frames lost, other 20 strict deltas and output-late delta zero. Operator noticed no physical disturbance. Both decks stopped, final evidence saved, scheduled task cleaned up after exit and heartbeat paused. Internal descriptor placement did not eliminate the inner-block skip pattern; final soak gate remains open. Prior INCOMPLETE record and ten-minute PASS retain their scope.

S2 candidate fc4ed608 moves experimental M3 ISO payloads into internal DMA memory; full host suite, M3 IDF 6.0.2 build, board check and signed-package checks pass. Local OTA installed exact clean image VALID ota_0, but old-image OTA restart produced boot406 PANIC and HS audio quarantine. Operator RESET restored normal boot407 and healthy S2 44100-Hz streaming. The 60.477-s dual MT/loop retest has telemetry and operator playback PASS: all22 strict deltas and output-late delta zero, both decks stopped. After track load cached DMA free is13995 B (minimum13087 B), so lifecycle/headroom remain qualification considerations. The subsequent independent 3600-s soak is strict ISO FAIL after529.276s/783samples: two packets and11frames lost, other20strict deltas zero, output-late delta1. Both decks stopped; operator noticed no disturbance. Evidence and post-failure diagnostics retained, checkpoint automation paused. Internal payload placement did not eliminate loss. Final soak acceptance and separate OTA-restart risk remain open; short PASS retains its scope.
