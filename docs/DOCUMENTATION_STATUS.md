# Documentation status

Status: **current P4-only source of truth, reconciled 2026-09-29**.

## Product boundary

[L software/candidate handoff](validation/FORK_IMPROVEMENTS_PACKAGE_L_SOFTWARE_20261005.md)
closes A-L software integration on the isolated branch. Board-specific signed
candidates and local channels preserve existing JC4880 format; runtime and tools
reject wrong project/version and ordinary tools exclude experimental images.
[First JC4880 L hardware session](validation/JC4880_L_CANDIDATE_20261005.md)
installed `M2.4-71-gd20a9f0e` in `ota_1`; AP/API/USB0/FLX4 startup and previous
design/controls passed focused checks. Operator heard clean MAIN/cue, but one
44-frame USB packet loss fails strict audio acceptance. Reproduction/correction,
relative resource comparison, reconnect/rollback and final soak stay open;
public M2.4 is unchanged. Diagnostic `M2.4-72-g875aefe0` identifies ISO SKIPPED;
the same packet-loss fault reproduced on H `M2.4-61-gc4912d5b` before I-L.
A short recovery A/B run was clean. The earlier isolation checkpoint used H
in `ota_0`; do not transfer its results to another candidate. JC1060 remains
software verified only.

The later heap-query diagnostic `M2.4-75-gb89c474c` was installed in `ota_1`,
boot 603, stopped after reproducing one SKIPPED packet under dense status
polling. PSRAM heap walks from periodic health/status are now a corrective
experiment; exact-image verification remains required. See the same hardware
report for source/artifact identities and failed runs. No public channel change.
The first correction `M2.4-76-g2941121b`, `ota_0`, boot 604 passed a 183-second
dense-status run, active resource comparison and operator MAIN/cue listening,
but the extended loop run stopped on one 12,994-us output-late block. USB/PCM
counters stayed zero. A 240/256-frame PCM5102A DMA/mixer geometry mismatch is
the next correction; exact-image acceptance remains open.

Current installed correction is `M2.4-77-g08996790`, `ota_1`, from pushed
source `0899679021bc7879fc244627a74af00d962be5ca`. Both local builds, the full
host runner and all eight exact-SHA CI jobs pass. Signed clean-build identity,
configuration, locks, hash and budget checks pass. Focused MAIN/D1/D2 cue
listening, active resource comparison, 183-second dense-status playback and
30-minute dual-loop playback pass with zero new strict faults. Periodic PSRAM
largest-block walks are absent; PCM5102A DMA and the mixer use the same
256-frame quantum without relaxing deadlines. This is focused fault-correction
evidence; full baseline, functional/lifecycle matrix, sink latency and final
180-minute qualification remain open. Public M2.4 is unchanged.

The subsequent 180-minute attempt is INCOMPLETE: the first scheduled checkpoint
found the host monitor absent with only approximately 29 minutes of saved
samples and no final result. The device was still playing with zero new live
USB/PCM/output-late counts. Both decks were deliberately stopped, artifacts
retained and the heartbeat paused. No reboot/counter reset or test restart was
performed; monitor lifetime must be verified before a new attempt. This does
not replace or extend the earlier completed 30-minute focused pass.

The operator subsequently approved a 151-minute continuation, crediting the
previous rounded 29 minutes. A detached Windows monitor survived its launching
shell, passed assertion self-tests and started on the same image at 10:25
Europe/Zagreb. It completed 9,060.489 seconds and 39,144 requests at 12:56,
with all five checkpoints and 897 saved active samples passing zero-new-fault
checks. Memory/stack reserves were stable and failed allocations zero. Both
decks are stopped on the same image; heartbeat automations are paused. The
operator-approved 29+151-minute accounting target is complete, but does not
prove uninterrupted 180-minute monitoring. Original interrupted evidence is
retained; final operator listening remains PENDING. Full functional/lifecycle
qualification, recovery baseline and new-hardware gates remain open.

The active product has one firmware and release target:
`firmware/main-deck-p4`. The ESP32-P4 owns USB0 Rekordbox storage, direct USB1
DDJ-FLX4 MIDI/audio, controller profiles and LEDs, dual-deck playback,
mixer/DSP, PCM5102A MAIN, FLX4 headphone cue, LVGL UI, Wi-Fi Remote and signed
OTA. No S3 firmware, UART control link or inter-board PCM bridge belongs to the
active product.

Compatibility names such as `control_link`, `S3CP` and `profile.s3bin` remain
in current code/file formats. They do not imply an active S3 processor.

The isolated development branch also has a separate JC1060 target sharing
the P4 core with its own identity, BSP, lock and CI build. [Package E evidence](validation/FORK_IMPROVEMENTS_PACKAGE_E_SOFTWARE_20261004.md)
does not qualify or release this board. All JC1060 physical gates are NOT RUN.
[Package I software closure](validation/FORK_IMPROVEMENTS_PACKAGE_I_SOFTWARE_20261005.md)
adds default-off Ethernet-only discovery, two-player claims and serialized
DBServer browse with a bounded owned Library cache. Software/mock/simulator
results do not qualify real CDJ or rekordbox peers. Incoming/touch/controller
LOADs respect the same lock/busy checks; no premature load ACK is sent.
[Package J software closure](validation/FORK_IMPROVEMENTS_PACKAGE_J_SOFTWARE_20261005.md)
adds verified SD downloads and owned full-identity assets through that shared
worker. Network/USB loss cannot stop completed SD playback. NFS supplies no
trustworthy volume UUID, so current mounts remain session-local: fresh downloads
are required and local cue edits cannot migrate across sessions. Host/mock/build
results do not qualify real peer/card performance or power-loss behavior.
[Package K software closure](validation/FORK_IMPROVEMENTS_PACKAGE_K_SOFTWARE_20261005.md)
adds a network-worker clock mailbox and deck-task sync follower with generation
checks at audio mutation. Master loss enters WAIT at the retained tempo; only
explicit PLAY/SYNC alignment may seek. Real phase and master handoff remain
NOT RUN. Latency defaults UNMEASURED and the engineering calibration is scoped
to sink/rate, held in RAM only. LOCKED is a model state, not physical acceptance.
[Package F](validation/FORK_IMPROVEMENTS_PACKAGE_F_SOFTWARE_20261004.md) adds
UAC1 packed 16/24-bit formats and USB MAIN/cue pacing. The service API selects
an available sink only with both decks stopped and recorder inactive. Host/build
evidence does not qualify DDJ-400 sound or FLX4 hardware regression.

## Production release

[Package H software closure](validation/FORK_IMPROVEMENTS_PACKAGE_H_SOFTWARE_20261004.md)
preserves the previous design in both native resolutions and removes duplicate
presentation trees. Seven simulator presentations, host suites and board builds
verify software only. Runtime memory, timing, touch, audio/reconnect, guarded
rollback and final 180-minute exact-image soak remain open.
[First ordinary H installation](validation/JC4880_H_CANDIDATE_20261004.md)
confirmed `M2.4-59-gf9260125` in `ota_0`, network/USB0/FLX4 presence and operator
confirmation of previous design/controls. Resource gate FAILED; focused correction
and exact-image requalification are required. No public release/channel changed.
The subsequent `M2.4-60-g6dfaf241` cleared allocation/HTTP stack failures, but
two paused loads failed the internal largest-block floor. Ordinary UI allocation
now uses PSRAM too; another fresh candidate is required. This supersedes earlier
statements that only preview uses custom LVGL allocation.
The previous installed H image was `M2.4-61-gc4912d5b` in `ota_0`. Both clean ordinary
builds/CI pass. Empty/two-paused-track absolute resource floors pass, allocation
failures zero; full baseline comparison and active/listening/soak gates stay open.
One idle soft reboot restored the same image, AP/API and both USB paths. Operator
confirmed previous design/FLX4 controls on this exact version after reboot.

[Package G](validation/FORK_IMPROVEMENTS_PACKAGE_G_SOFTWARE_20261004.md)
adds an explicit recorder experiment and default-off SD idle workaround.
Ordinary release builds remain recorder-free and retain original SD idle wait.
Software tests/builds do not establish SD performance, power-loss durability
or audible acceptance; all physical G gates are NOT RUN.

The isolated fork-improvements branch has software-verified packages A, B and
C on JC4880. [Package B closure](validation/FORK_IMPROVEMENTS_PACKAGE_B_SOFTWARE_20261004.md)
records B1-B19 and all physical gates as NOT RUN. This is development evidence,
not an installation, hardware acceptance or change to the release below.
[Package D software closure](validation/FORK_IMPROVEMENTS_PACKAGE_D_SOFTWARE_20261004.md)
adds S3CP v4 and DDJ-400 MIDI, coordinated with Web exporter `bc47c3c`.
Physical DDJ-400/UAC acceptance and public Web deployment remain NOT RUN.

| Item | M2.4 value |
| --- | --- |
| Annotated tag | `M2.4` |
| Frozen source | `9d0c954fc502ae237fabedb764368cd9b10f10dc` |
| Tag object | `c106f918e3c0919421481cca12ecf94ad7af49fa` |
| Toolchain | ESP-IDF v6.0.2 |
| Installed release record | `M2.4`, `ota_1`, boot 560, reset `POWERON`, OTA `idle` and empty `last_error` |
| Application | 2,505,264 bytes; SHA-256 `1bc85aaa2ee26fc5c183ef673e72f6017d87539f4ddd9032f8d403c5bcc2da52` |
| Signed OTA bundle | 2,505,452 bytes; SHA-256 `c76b9160bdf9757e04b4b44e92a4f034bf6e913ac35caef7b4fa12092c3e35c4` |
| Public channel | `https://ota.pajoniiir.eu` |
| GitHub Release | `https://github.com/dvucinozd/Pajoniiir/releases/tag/M2.4` |

The installation identity above records the accepted release session; it is
not a claim about a later live boot unless `/api/firmware` is checked again.
Commits after the immutable tag are maintenance/development commits and do not
change the published M2.4 artifact.

## Acceptance summary

M2.4 inherits the completed M2.1 P4 qualification, the M2.2 Wi-Fi Remote
release and the post-review maintenance qualification. Its exact-tagged build,
signature/package verification, signed opposite-slot installation, live
cross-signed production TLS probes, cold-boot USB0/FLX4/UAC telemetry smoke,
dual-deck playback and operator-confirmed clean MAIN/cue, public-channel verification and GitHub asset
round-trip verification passed. The production release record is
[`validation/M2_4_PRODUCTION_RELEASE_20260929.md`](validation/M2_4_PRODUCTION_RELEASE_20260929.md).

The retained supporting evidence covers:

- complete lifecycle accounting: 43 PASS, seven explicitly waived I/J cycles,
  zero pending and 39 accepted physical attachment/reconnect actions;
- real MP3/WAV/FLAC playback, FAT32/exFAT and superfloppy/MBR/GPT media;
- dual Master Tempo remediation, UAC idle continuity and a clean
  180.156-minute combined soak with operator-confirmed audio;
- public pull OTA, interrupted-transfer recovery, signed rollback and
  post-reboot dual-USB recovery;
- accepted common 5 V and protected dual-VBUS bench/enclosure wiring.

## Inherited maintenance qualification and accepted exclusions

Merge image `M2.2-37-g751d3c6` at `751d3c6` has passed a separate
180.058-minute dual-deck soak with operator-confirmed MAIN/cue audio, real-media
load timing, a 324-row catalog reboot/remount check, slow-network mobile
viewport emulation and an API-level MAIN meter decay precheck. A default-OFF
instrumented build from `07618a5` subsequently passed 100 forced PCM timeline
wrap/handoff iterations, a 1 us maximum publication critical section and a
paired live three-minute dual-deck runtime smoke. The release candidate was
then restored and verified on `ota_0`. Its physical-phone Web Remote follow-up
also passed real-track title/PLAY state and visible MAIN meter decay, reaching
zero 788 ms after STOP. This evidence is inherited by M2.4 because its release
delta after M2.3 is limited to TLS certificate-bundle configuration.

On 2026-09-28 the same candidate passed duplicate raw track-ID isolation across
two independently generated Rekordbox exports. Both exposed `track_key=1`,
while Hot Cue pad 8 retained separate 11000 ms and 22000 ms positions across
media remount; A also retained its cue across software boot `553 -> 554`.
Cleanup was operator-confirmed on both media and strict device health remained
clean. The evidence is
[`validation/P4_DUPLICATE_TRACK_ID_ACCEPTANCE_20260928.md`](validation/P4_DUPLICATE_TRACK_ID_ACCEPTANCE_20260928.md).

On 2026-09-28 the operator explicitly accepted the deferred real Rekordbox cue
A/C and loop slot/time comparison and repetition of the duplicate raw track-ID
gate on the exact M2.3/M2.4 binary as outside the release scope. Both remain NOT RUN
and are not represented as passes; the duplicate-ID behavior retains its PASS
on the `M2.2-37-g751d3c6` maintenance candidate. With those recorded
limitations, the candidate was approved for the exact-tag release build,
installation and qualification sequence. The original decision is
[`validation/M2_3_RELEASE_DECISION_20260928.md`](validation/M2_3_RELEASE_DECISION_20260928.md).

The exact broader qualification status is in
[`validation/P4_POST_REVIEW_RELEASE_QUALIFICATION_20260925.md`](validation/P4_POST_REVIEW_RELEASE_QUALIFICATION_20260925.md)
and
[`validation/P4_PCM_TIMELINE_SCHEDULER_PROBE_20260927.md`](validation/P4_PCM_TIMELINE_SCHEDULER_PROBE_20260927.md).
The execution procedure is
[`validation/P4_DUPLICATE_TRACK_ID_ACCEPTANCE_PROCEDURE.md`](validation/P4_DUPLICATE_TRACK_ID_ACCEPTANCE_PROCEDURE.md).

## Accepted limitations

- I3-I5 and J2-J5 remain operator-waived; they are not reported as passes.
- Numeric enclosure thermal/RF/strain margins were not captured. Repeat
  qualification after any enclosure, wiring, supply or RF-layout change.
- The shared service credential is accepted for this deployment. WPA2/WPA3
  transition mode with PMF capability is enabled; per-device credentials and
  WPA3-only/PMF-required mode remain future hardening.
- Secure Boot, Flash Encryption and security eFuse provisioning are disabled
  because the sole board has no sacrificial provisioning/recovery pilot.
- Encrypted offline primary and separate encrypted backup signing-key copies
  are operator-confirmed. Recovery signing from the backup remains deferred
  maintenance, not completed evidence.
- The recorder is compiled out. Non-FLX4 profiles are host evidence only.
- Real Rekordbox cue A/C and loop slot/time comparison is not run for M2.4;
  the operator accepted it as outside the current deployment scope.
- Exact-tag repetition of duplicate raw track-ID isolation is not run for M2.4;
  the passing result remains tied to `M2.2-37-g751d3c6`.
- The M2.4 signed OTA software reboot did not enumerate USB0/USB1. Boot 560
  after a full power cycle restored storage, FLX4 MIDI/UAC and clean playback;
  automatic post-OTA dual-root recovery is a follow-up item.

## Source-of-truth order

1. active firmware, tests, build configuration and immutable release artifacts;
2. this status, `PROJECT_OVERVIEW.md`, `ARCHITECTURE.md`,
   `HARDWARE_WIRING.md`, `OTA-UPDATE.md` and
   `SECURITY_PROVISIONING_POLICY.md`;
3. `STARTUP_CHECKLIST.md`, `DEVELOPMENT_PLAN.md` and `RISK_REGISTER.md`;
4. retained dated validation records;
5. Git history for removed superseded plans and intermediate evidence.
