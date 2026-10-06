# Pajoniiir documentation

Status: **reconciled 2026-10-06; M2.5 published; A-L merged into master**.

Start with the [user manual](index.html) for operation and
[DOCUMENTATION_STATUS.md](DOCUMENTATION_STATUS.md) for current support.
The published configuration is JC4880 / DDJ-FLX4 / PCM5102A MAIN / FLX4 cue,
frozen at `M2.5`. JC1060/DDJ-400/real Link peers remain
software-verified development configurations. No S3 firmware is active.

The [M2.5 release record](validation/M2_5_RELEASE_20261006.md) records final
artifacts, publication, short hardware/listening checks and the explicit waiver
of a new long soak. Historical v91 evidence remains tied to v91.

## Operation and maintenance

| Document | Purpose |
| --- | --- |
| [User manual](index.html) | Current JC4880/FLX4 controls, media, network and maintenance |
| [Project overview](PROJECT_OVERVIEW.md) | Capabilities, topology, published vs development scope |
| [Startup checklist](STARTUP_CHECKLIST.md) | Normal operation, new-image gates and explicit NOT RUN variants |
| [Hardware wiring](HARDWARE_WIRING.md) / [JC4880 pinout](../firmware/main-deck-p4/PINOUT_P4.md) | Accepted topology; requalify after wiring/supply changes |
| [OTA procedure](OTA-UPDATE.md) | Signed push/pull, startup confirmation, board identity and recovery |
| [Security policy](SECURITY_PROVISIONING_POLICY.md) | Signing-key custody, service exposure and deferred hardware provisioning |
| [Risk register](RISK_REGISTER.md) | Current accepted limits, exceptions and future qualification |

## Development

| Document | Purpose |
| --- | --- |
| [Architecture](ARCHITECTURE.md) | Shared P4 core, ownership, workers, USB/audio/UI/network boundaries |
| [Development plan](DEVELOPMENT_PLAN.md) | Change-driven maintenance and remaining hardware work |
| [Integration/provenance](FORK_IMPROVEMENTS.md) | A-L implementation, collaborator [kayrozen](https://github.com/kayrozen), frozen donor SHA and licenses |
| [Controller schema](CONTROLLER_PROFILE_SCHEMA.md) / [upload procedure](CONTROLLER_PROFILE_UPDATE.md) | JSON v1 and S3CP v2/v3/v4, compatibility and activation |
| [FLX4 mapping](DDJ_FLX4_MIDI_MAP.md) | Verified MIDI addresses and per-control historical acceptance |
| [Hercules candidate](HERCULES_INPULSE_500_MIDI_MAP.md) / [DDJ-400 candidate](../controllers/pioneer_ddj_400/README.md) | Software profile evidence; hardware NOT RUN |
| [Rekordbox format](rekordbox-format-analysis.md) | PDB/ANLZ interpretation, current bounds and historical observations |
| [JC1060 build guide](../firmware/main-deck-jc1060/README.md) | Separate entrypoint/lock, native layout, Ethernet and unrun bring-up |
| [Simulator guide](../tests/ui_simulator/README.md) | Actual product/preview/Link presentation regression modes |
| [M2.5 artwork fallback](validation/M2_5_DEFAULT_ARTWORK_20261006.md) | User-supplied logo, software checks, signed candidate OTA and focused JC4880 display/audio acceptance |
| [JC4880 library smoke](validation/JC4880_LIBRARY_SMOKE_20261006.md) | Focused v91 catalog/load, artwork/memory-cue visibility and playlist navigation checks |
| [LIBAPTA plan](LIBAPTA_P4_INTEGRATION_PLAN.md) | Deferred separate integration; not current firmware capability |

## Current release evidence

- [Published v91 record](validation/JC4880_V91_RELEASE_20261005.md):
  frozen source, artifact hashes, signatures, publication and accepted scope.
- [v91 review](validation/JC4880_V91_RELEASE_REVIEW_20261005.md):
  focused physical/listening results, rollback and remaining limits.
- [JC4880 hardware ledger](validation/JC4880_L_CANDIDATE_20261005.md):
  chronological failures, corrections and exact-image tests.
- [L software handoff](validation/FORK_IMPROVEMENTS_PACKAGE_L_SOFTWARE_20261005.md):
  both target builds, locks, image budgets, CI and packaging.
- [Documentation audit](validation/DOCUMENTATION_RECONCILIATION_20261006.md):
  complete tracked-document inventory and this reconciliation.

The accepted v91 soak is **180m01.37s in two segments** with a recorded interruption.
It is an explicit operator-approved release exception.
**Uninterrupted 180-minute v91 qualification remains NOT RUN.**

## Historical evidence

Dated validation records apply to the versions/scenarios they name. A prior
PASS does not qualify changed code, and an older open gate does not reopen a
later documented focused pass. Their results and hashes remain preserved.

- [M2.1](validation/M2_1_PRODUCTION_RELEASE_20260920.md),
  [M2.2](validation/M2_2_PRODUCTION_RELEASE_20260923.md),
  [M2.3 decision](validation/M2_3_RELEASE_DECISION_20260928.md),
  [M2.4](validation/M2_4_PRODUCTION_RELEASE_20260929.md):
  immutable earlier release/decision records.
- [USB lifecycle](validation/P4_DUAL_USB_LIFECYCLE_MATRIX_20260911.md) and
  [power/VBUS](validation/P4_POWER_VBUS_ACCEPTANCE_20260911.md):
  per-image lifecycle accounting and unchanged-wiring acceptance.
- [Media cache](validation/P4_BOUNDED_MEDIA_CACHE_20260919.md),
  [exFAT/GPT](validation/P4_USB_EXFAT_GPT_SMOKE.md),
  [duplicate track IDs](validation/P4_DUPLICATE_TRACK_ID_ACCEPTANCE_20260928.md):
  earlier local-media evidence; exact-v91 repetition is not implied.
- [Master Tempo](validation/P4_DUAL_MASTER_TEMPO_WDT_20260919.md),
  [UAC continuity](validation/P4_UAC_IDLE_CONTINUITY_REMEDIATION_20260920.md),
  [earlier combined soak](validation/P4_FINAL_COMBINED_SOAK_20260920.md),
  [OTA fault matrix](validation/P4_PULL_OTA_FAULT_MATRIX_20260920.md):
  historical qualification and remediation.
- [Post-review qualification](validation/P4_POST_REVIEW_RELEASE_QUALIFICATION_20260925.md),
  [scheduler probe](validation/P4_PCM_TIMELINE_SCHEDULER_PROBE_20260927.md),
  [supply investigation](validation/P4_M2_5_USB_REBOOT_INVESTIGATION_20261001.md),
  [M2.5 handoff](M2_5_HANDOFF_20261002.md):
  older sessions; their installed versions/channel states are historical.
- [H candidate](validation/JC4880_H_CANDIDATE_20261004.md):
  failed resource candidates and corrective sequence, not accepted release images.

Older removed S3/dual-processor documents remain in Git history. The audit
inventory indexes every retained record, including the package B-K closures.

## Documentation checks

From the repository root:

```powershell
.\tools\check_documentation.ps1
git diff --check
git status --short
```

Documentation integrity checks Markdown/HTML links, assets and retired paths.
Changes under `docs/` also have a separate Pages deployment gate; current
source changes do not establish that the hosted manual has already deployed.
