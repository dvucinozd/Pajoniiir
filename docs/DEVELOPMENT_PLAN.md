# P4 post-release development plan

Status: **M2.4 released; M2.5 USB recovery and additional-controller work**.

## Current baseline

The frozen JC4880/FLX4 v91 acceptance is closed with an explicit operator-
accepted segmented-soak exception. Physical unconfirmed-image rollback and
the 60-second startup readiness rejection passed. Publication is authorized
and in progress; the [release record](validation/JC4880_V91_RELEASE_20261005.md)
is authoritative for current status and remaining unrun variants. The original
tested image is retained; diagnostic-only source changes are not that release.
Historical package checkpoints below do not reopen completed focused gates.

[Package G](validation/FORK_IMPROVEMENTS_PACKAGE_G_SOFTWARE_20261004.md)
provides isolated SD idle/DMA A/B experiments, bounded diagnostics and recorder
fault/ownership tests. Regular builds keep recorder and SD wait experiments off.
Card latency, fault injection and MAIN/cue listening remain NOT RUN; acceptance
requires exact-image measurements.
[H software closure](validation/FORK_IMPROVEMENTS_PACKAGE_H_SOFTWARE_20261004.md)
retains the previous design in both native layouts, with artwork/key/status,
effective hot cues and memory cues. Touch holds, deletion and explicit restore
use the shared semantic path. Only the selected presentation is constructed;
both modes use the same Settings. The dj_ui preview remains optional/default-off.
H3 installation failed Wi-Fi/USB acceptance due to internal heap pressure;
Recovery is complete. Pending OTA startup now waits for saved-enabled AP/HTTP
readiness within 60 seconds before confirmation; failure requests rollback.
Runtime allocation, startup-phase, heap and critical-stack diagnostics now have
host coverage and an executable physical evidence gate. Runtime memory and
exact-image acceptance remain open; no production channel change follows from
software verification. Continue I-L software integration. The operator deferred
physical audio tests and the final soak until the end of integration; these
remain mandatory exact-image gates before product acceptance or publication.
[I1-I3](validation/FORK_IMPROVEMENTS_PACKAGE_I_PROGRESS_20261004.md) add the MIT
codec, bounded peer/epoch model, a shared two-player claim coordinator and a
default-off JC1060 Ethernet worker. UDP sockets require the actual Ethernet
netif; no Wi-Fi/default-interface fallback exists.
[I software closure](validation/FORK_IMPROVEMENTS_PACKAGE_I_SOFTWARE_20261005.md)
includes the nonblocking Ethernet-bound TCP adapter, serialized DBServer worker,
owned 2,000-row cache, visible metadata and Library source/folder/playlist bridge.
Incomplete lists and stale epochs never publish completed rows. Touch/controller
and incoming loads share stopped/busy admission. Real UDP/TCP sanitizers and the
Link Library simulator pass. Physical peer interoperability is NOT RUN.
[J software closure](validation/FORK_IMPROVEMENTS_PACKAGE_J_SOFTWARE_20261005.md)
adds interface-bound NFS, verified SD artifacts/manifests, full identity-bound
analysis/artwork/cues and one common load worker. Cancellation/space/recording
admission failures preserve existing decks; local SD playback is independent of
network and USB media presence. Unknown volume identity requires fresh session
downloads and prevents cross-session local cue migration. The A/B replacement,
integrity/interruption, gated SD loader and UI admission regressions are automated.
Physical SD/peer/concurrent playback acceptance is NOT RUN.
[K software closure](validation/FORK_IMPROVEMENTS_PACKAGE_K_SOFTWARE_20261005.md)
adds the 40 ms clock follower, session-fenced audio mutations, sticky master
selection, acknowledged handoff and own status/beats. Only explicit PLAY/SYNC
may align with a seek; loss retains tempo in WAIT. A measured latency applies
only to its sink/rate, and no measurement is invented. Physical output phase,
listening and real handoff remain NOT RUN.
[L candidate handoff](validation/FORK_IMPROVEMENTS_PACKAGE_L_SOFTWARE_20261005.md)
completes software integration A-L. Ordinary JC4880/JC1060 artifacts are isolated
by signed project, with fixed image budget, exact source/CI/config/lock evidence
and separate local channel documents. Preview/recorder/storage experiments are
excluded. JC4880 hardware qualification has started; keep JC1060/DDJ-400/real
peer acceptance NOT RUN until available. The
[current hardware record](validation/JC4880_L_CANDIDATE_20261005.md) retains
failed L/H runs and the installed corrective `M2.4-77-g08996790`. Its focused
MAIN/cue listening, resource comparison and 30-minute dense-status dual-loop
test pass with zero new strict faults. Remaining functional/lifecycle gates,
the missing recovery resource baseline and final 180-minute soak stay open.
The operator-approved 29+151-minute segmented telemetry target is now complete:
the detached continuation passed 9,060.489 seconds with zero new strict faults
and stable reserves. The original interruption is retained; uninterrupted
180-minute monitoring is not claimed. Final listening is operator-confirmed PASS;
the remaining functional/lifecycle/recovery-baseline gates are open; no release follows.

[First candidate installation](validation/JC4880_H_CANDIDATE_20261004.md):
`M2.4-59-gf9260125` returned network, USB0 and FLX4 after signed OTA. Operator
confirmed previous design/controls. Resource acceptance failed (two waveform
internal-DMA probes and HTTP stack reserve 456 bytes). Correct allocation/stack
ownership and repeat on a fresh pushed-source image before continuing acceptance.
The first correction removed failed allocations and restored HTTP stack reserve.
Two paused loads still failed the 12 KiB internal largest-block floor, so ordinary
LVGL moves to the tested PSRAM-only allocator. Repeat exact-image measurements;
do not waive or lower the gate to make the candidate pass.
The H checkpoint `M2.4-61-gc4912d5b` was installed with ordinary PSRAM LVGL.
Both clean regular builds and eight CI jobs pass. Empty/two-paused-track absolute heap/stack floors
pass with zero allocation failures. Recovery largest-block baseline, active
audio/timing and final soak remain open; see the candidate record for raw evidence.

The separate [fork improvements program](FORK_IMPROVEMENTS.md) tracks the
JC1060/DDJ-400/Link port and current package status. It starts from M2.5
`05296b8` and does not merge the separate APTA branch or change M2.4 production.
The [2026-10-04 software handoff](validation/FORK_IMPROVEMENTS_HANDOFF_20261004.md)
records pushed steps A and B1–B5 and the hardware gates left unrun.
The live [integration log](FORK_IMPROVEMENTS.md) records B1-B19 and C1-C5.
[Package B software closure](validation/FORK_IMPROVEMENTS_PACKAGE_B_SOFTWARE_20261004.md)
includes the bounded MP3 frame index and jog profile producers. Packages B and C
are software verified on JC4880; physical playback, artwork and touch acceptance
remain NOT RUN. [Package D software closure](validation/FORK_IMPROVEMENTS_PACKAGE_D_SOFTWARE_20261004.md)
covers v4 parser/validators/runtime/compiler, DDJ-400 MIDI and coordinated Web
export. [Board package E](validation/FORK_IMPROVEMENTS_PACKAGE_E_SOFTWARE_20261004.md)
adds JC1060 BSP/entrypoint, shared peripherals, separate lock and CI build.
Physical bring-up remains NOT RUN; layouts belong to H.
[Package F](validation/FORK_IMPROVEMENTS_PACKAGE_F_SOFTWARE_20261004.md) adds
descriptor-selected four-channel UAC1 and consumer-paced USB MAIN/cue, preserving
the default JC4880 PCM5102A path. DDJ-400 audio and FLX4 regression acceptance
remain NOT RUN. Packages G/H are software verified; production remains unchanged.

- Production tag: `M2.4`
- Frozen source: `9d0c954fc502ae237fabedb764368cd9b10f10dc`
- Required toolchain: ESP-IDF v6.0.2
- Active firmware target: `firmware/main-deck-p4`
- Current architecture: direct P4 USB0 media plus USB1 FLX4 MIDI/UAC

The release qualification, lifecycle matrix, mixed-format playback, final
combined soak, OTA fault paths and exact-image product smoke are complete.
Do not reopen a closed gate without a reproducible failure, a changed
assumption or an explicit new release requirement.

## Maintenance rule

Every change starts from its impact, not from the age of the previous test:

| Change area | Minimum evidence before release |
| --- | --- |
| Documentation/media only | `tools/check_documentation.ps1`, `git diff --check`, Documentation integrity CI and Pages review when `index.html` changes |
| Host-only tooling/test | affected suite plus complete P4 host suite when shared behavior changes |
| Firmware logic | complete P4 host suite and ESP-IDF v6.0.2 build |
| UI | UI simulator E2E plus firmware build; physical display/touch smoke for release |
| USB/power/audio/OTA | relevant focused hardware regression plus exact-image product smoke |
| DSP, scheduler or buffering | focused fault/counter test and an appropriately long audible hardware run |
| Partition, bootloader or trust key | wired recovery plan, isolated signed build and supervised hardware installation |

Preserve firmware version, source commit, slot, boot identity, strict counter
deltas and operator-visible/audible results in new validation records.

## Completed M2.4 scope

M2.4 freezes the post-M2.2 production-review fixes and the narrow TLS
certificate-bundle correction from PR #43. The latter enables validation of
valid cross-signed public chains while retaining mandatory CA and hostname
verification. The exact tagged build, signed package, physical installation,
pre-public and post-public production pull probes, cold-boot hardware telemetry
smoke and GitHub/public asset round trips passed. The release record is
[`validation/M2_4_PRODUCTION_RELEASE_20260929.md`](validation/M2_4_PRODUCTION_RELEASE_20260929.md).

The signed OTA software reboot did not enumerate USB0/USB1; a full power cycle
restored both roots and all product functions. Preserve the cold-cycle operator
step for the current release and investigate unattended post-OTA recovery before
claiming it for a later release.

## Completed M2.2 scope

M2.2 is a focused Wi-Fi Remote upgrade. It preserves the M2.1 USB, playback,
audio, OTA and controller topology while replacing the embedded HTML/CSS/JS
operator surface with a responsive dual-deck console.

The implementation contract is:

- the page remains fully embedded and has no runtime CDN, webfont or framework
  dependency;
- P4 `deck_core` remains authoritative for playback and SYNC state;
- continuous browser gestures may preview locally, but decoder seek is committed
  once per completed gesture;
- every mutation remains a marked POST request and must handle non-2xx responses;
- service cards expose existing signed OTA, network and controller-profile APIs
  without weakening their confirmations or credential boundaries;
- desktop, phone landscape and phone portrait layouts receive automated contract
  coverage plus visual review;
- release acceptance requires the complete P4 host suite, ESP-IDF v6.0.2 build,
  OTA installation and a focused physical Wi-Fi Remote playback/seek/mixer/OTA
  smoke on the exact candidate image.

All implementation gates above passed on commit `2c2ec32`. The exact `M2.2`
tag was built, signed, installed on `ota_0`, hardware/API-smoked and published
through both the public pull channel and GitHub Releases. The release record is
[`validation/M2_2_PRODUCTION_RELEASE_20260923.md`](validation/M2_2_PRODUCTION_RELEASE_20260923.md).

## M2.5 scope and execution order

1. Reproduce and fix automatic dual-root recovery after a software/OTA reboot.
   Preserve the shared recovery arbiter and active-enumeration exclusion.
   The [2026-10-01 investigation](validation/P4_M2_5_USB_REBOOT_INVESTIGATION_20261001.md)
   records the weak-supply reboot loop and successful software-reboot/signed-OTA
   telemetry on unchanged M2.4 after supply replacement. No firmware fix is
   currently justified; operator MAIN/cue and physical-control confirmation
   also passed. Repeat recovery acceptance on the final M2.5 image.
2. Qualify additional real DJ controllers. Existing profile files, including
   Hercules DJControl Inpulse 500, are candidates, not accepted hardware.
   Freeze the supported-device list from descriptor, MIDI, LED, reconnect and
   applicable audio evidence; preserve FLX4 regression acceptance.
   [CC7 scaling implementation](validation/P4_M2_5_CC7_SCALING_20261002.md)
   adds v3 mixer/tempo input support with v2 compatibility. Development OTA,
   v3 profile persistence and operator FLX4 MAIN/cue smoke passed; physical
   non-FLX4 acceptance remains open.
3. Qualify the exact release image, including signed opposite-slot OTA and
   automatic USB0/USB1 return without manual power cycling, MAIN and cue.

## Other future work

These remain maintenance projects or deferred product extensions. Controller
qualification and post-OTA recovery below are now assigned to M2.5 above:

1. **Non-FLX4 controller qualification.** Obtain real hardware and capture
   descriptors, MIDI, LEDs, reconnect behavior and four-channel audio before
   advertising any additional controller profile.
2. **Recorder qualification.** Keep `CONFIG_AUDIO_RECORDER_ENABLED` disabled
   until SD latency, removal, capacity, finalize/power-loss and combined audio
   load are physically qualified.
3. **Security hardening pilot.** Before a later production batch, qualify
   Secure Boot v2, release-mode Flash Encryption, eFuse provisioning and full
   wired recovery on a sacrificial P4 board.
4. **Service credentials.** Consider per-device credentials and
   WPA3-only/PMF-required mode if deployment expands beyond the current
   controlled environment.
5. **Signing-key rotation.** Verify the offline backup through a disposable
   signature test, then implement successor-key overlap before retiring
   `rel-001`.
6. **LIBAPTA integration.** Begin only after the upstream embedded profile and
   release gates in [`LIBAPTA_P4_INTEGRATION_PLAN.md`](LIBAPTA_P4_INTEGRATION_PLAN.md)
   are satisfied.
7. **Post-OTA USB recovery.** Reproduce why a signed OTA software reboot on
   M2.3/M2.4 left both roots unenumerated until a full power cycle, then restore
   automatic dual-root recovery without weakening the recovery arbiter.

## Release discipline

- Never move published tags.
- Build a release from the exact clean commit that will be tagged.
- Run automated gates before tagging and exact-image hardware acceptance before
  publication.
- Publish immutable versioned OTA content before switching `latest.json`.
- Keep signing keys and hosting credentials outside Git and release artifacts.
- Retain wired recovery until any new hardware-rooted security configuration
  has been independently proven.
