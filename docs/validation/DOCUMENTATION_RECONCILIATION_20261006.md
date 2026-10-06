# Documentation reconciliation — 2026-10-06

Status: **PASS — documentation reconciled; firmware unchanged**.

## Scope and authority

Reviewed the repository's **78 existing tracked Markdown/HTML files** for
current-vs-historical status, public product identity, board/controller scope,
technical contracts and local links. This includes module/controller/test guides,
the Pages manual, deferred APTA plan, official MIDI reference and dated evidence.
The embedded web HTML is a runtime asset; it was inspected but not modified.
Two guides are added: this inventory and the JC1060 entrypoint guide, bringing
the documentation inventory to **80** files.

The source before this edit was master `c352158af0292d82d929119cea288b12ae922812`,
identical to origin/master. No other worktree, firmware source, configuration,
dependency lock, tag or published firmware artifact was changed.

Current hardware/publication authority is the
[frozen v91 release](JC4880_V91_RELEASE_20261005.md), source
`75136aef749a1f03d9089c8b6ff6452b3dba0839`. During this review the remote
GitHub release was confirmed non-draft/non-prerelease, the local release tag
peeled to the frozen SHA, and live HTTPS `latest.json` still advertised
v91 / 2,570,940 bytes /
`9810b31ece270c7b406be2eb605331ad460e83dc06134519e57498cb6270d069`.
This metadata refresh is not a new device observation, audio test or pull-OTA
installation. Prior publication evidence retains artifact signature/hash checks.

## Reconciliation

- Replaced accumulated contradictory checkpoint prose in status, checklist,
  development plan and risks with current scope and links to unchanged records.
- Updated overview/index/manual to published v91 and the master merge, retaining
  the explicit segmented-soak exception and all unrun configurations.
- Corrected architecture: two board entrypoints, one P4 core, worker-owned
  profile initialization, one UI tree, PSRAM ownership, shared SD admission.
- Removed the old universal post-OTA power-cycle instruction. It remains a
  historical bare-M2.4 observation; current recovery claims name their scenarios.
- Corrected controller docs to JSON v1 / S3CP v2/v3/v4, raw 8/9, SysEx/scaling/
  filter flag, descriptor-driven audio bounds and rollback compatibility.
- Corrected Rekordbox title index 17, PQTZ downbeat 1, PWV4, memory cues,
  parser bounds, artwork and actual ownership/API references.
- Corrected mobile manual overflow and clipped hero content with bounded grid
  sizing, wrapping long codes and a smaller mobile logo; visual review passed.
- Updated simulator guide for all eight modes and the JC1060 build guide for
  separate locks/project/channel, USB roles, Ethernet pin conflicts and NOT RUN.
- Credited collaborator [kayrozen](https://github.com/kayrozen), frozen donor
  `428b97dd4a175f03d3a172c8db9c4d5ed94195fb`, and retained upstream licenses
  and original references.
- Restored the original CodeRabbit badge beside the README badges after the
  operator identified its accidental removal in the previous header cleanup.
- Added scope notices to 40 older/scenario records without rewriting their
  measured results, PASS/FAIL/NOT RUN entries or artifact hashes.

## Preserved limits

v91 is accepted for JC4880/FLX4 with PCM5102A MAIN and FLX4 cue. Its saved
10,801.3664176 seconds across two segments is an operator-approved release
exception; uninterrupted 180-minute qualification remains NOT RUN. Later
fault-injection source/images are diagnostic-only, not a re-versioned v91.

New-board/controller/real-peer Link, untested local metadata/touch/shift/
source-cue/cancellation variants, experimental recorder/SD and security pilot
gates remain open as described in [current status](../DOCUMENTATION_STATUS.md).
APTA and public Web Profile Builder deployment remain separate.
Historical evidence is not silently promoted to current hardware acceptance.

## Checks

| Gate | Result |
| --- | --- |
| Full Markdown/HTML local links, assets and retired paths | PASS: 80 files, 134 retired paths |
| Whitespace / documentation-only change scope | PASS; selective staging checked before commit |
| HTML anchors and responsive desktop/mobile visual review | PASS: 1440px and 390px, no horizontal overflow; all five lazy-loaded images load; internal anchors resolve |
| Original dated results preserved except scope notices | PASS: removing only new notices reproduces all 40 original records exactly (normalized line endings) |
| Firmware build / host audio suites / physical tests | NOT RUN: documentation-only, firmware unchanged |
| Pages hosting | Existing workflow deploys docs on master push; verify completion separately |

Browser captures and CLI output are retained outside Git at
`D:/Documents/.codex-reviews/Pajoniiir-documentation-20261006/`;
they are not release artifacts. Selective documentation commit/
push must verify local/remote SHA. No new firmware publication is performed.

## Complete inventory

| Path | Classification | Review disposition |
| --- | --- | --- |
| [README.md](../../README.md) | Current operation/development | Reconciled or retained where already accurate |
| [controllers/ni_traktor_s2_mk3_basic/README.md](../../controllers/ni_traktor_s2_mk3_basic/README.md) | Current operation/development | Reconciled or retained where already accurate |
| [controllers/pioneer_ddj_400/README.md](../../controllers/pioneer_ddj_400/README.md) | Current operation/development | Reconciled or retained where already accurate |
| [docs/ARCHITECTURE.md](../../docs/ARCHITECTURE.md) | Current operation/development | Reconciled or retained where already accurate |
| [docs/CONTROLLER_PROFILE_SCHEMA.md](../../docs/CONTROLLER_PROFILE_SCHEMA.md) | Current operation/development | Reconciled or retained where already accurate |
| [docs/CONTROLLER_PROFILE_UPDATE.md](../../docs/CONTROLLER_PROFILE_UPDATE.md) | Current operation/development | Reconciled or retained where already accurate |
| [docs/DDJ_FLX4_MIDI_MAP.md](../../docs/DDJ_FLX4_MIDI_MAP.md) | Current operation/development | Reconciled or retained where already accurate |
| [docs/DEVELOPMENT_PLAN.md](../../docs/DEVELOPMENT_PLAN.md) | Current operation/development | Reconciled or retained where already accurate |
| [docs/DOCUMENTATION_STATUS.md](../../docs/DOCUMENTATION_STATUS.md) | Current operation/development | Reconciled or retained where already accurate |
| [docs/FORK_IMPROVEMENTS.md](../../docs/FORK_IMPROVEMENTS.md) | Current operation/development | Reconciled or retained where already accurate |
| [docs/HARDWARE_WIRING.md](../../docs/HARDWARE_WIRING.md) | Current operation/development | Reconciled or retained where already accurate |
| [docs/HERCULES_INPULSE_500_MIDI_MAP.md](../../docs/HERCULES_INPULSE_500_MIDI_MAP.md) | Current operation/development | Reconciled or retained where already accurate |
| [docs/LIBAPTA_P4_INTEGRATION_PLAN.md](../../docs/LIBAPTA_P4_INTEGRATION_PLAN.md) | Deferred separate plan | Excluded from v91/A-L; upstream not requalified |
| [docs/M2_5_HANDOFF_20261002.md](../../docs/M2_5_HANDOFF_20261002.md) | Historical / scenario evidence | Added scope/current-status pointer; original results retained |
| [docs/OTA-UPDATE.md](../../docs/OTA-UPDATE.md) | Current operation/development | Reconciled or retained where already accurate |
| [docs/PROJECT_OVERVIEW.md](../../docs/PROJECT_OVERVIEW.md) | Current operation/development | Reconciled or retained where already accurate |
| [docs/README.md](../../docs/README.md) | Current operation/development | Reconciled or retained where already accurate |
| [docs/RISK_REGISTER.md](../../docs/RISK_REGISTER.md) | Current operation/development | Reconciled or retained where already accurate |
| [docs/SECURITY_PROVISIONING_POLICY.md](../../docs/SECURITY_PROVISIONING_POLICY.md) | Current operation/development | Reconciled or retained where already accurate |
| [docs/STARTUP_CHECKLIST.md](../../docs/STARTUP_CHECKLIST.md) | Current operation/development | Reconciled or retained where already accurate |
| [docs/index.html](../../docs/index.html) | Current operation/development | Reconciled or retained where already accurate |
| [docs/reference/DDJ-FLX4_MIDI_message_List.md](../../docs/reference/DDJ-FLX4_MIDI_message_List.md) | Authoritative external reference | Retained; MIDI addresses unchanged |
| [docs/rekordbox-format-analysis.md](../../docs/rekordbox-format-analysis.md) | Current operation/development | Reconciled or retained where already accurate |
| [docs/validation/CODE_REVIEW_M2_2_20260923.md](../../docs/validation/CODE_REVIEW_M2_2_20260923.md) | Historical / scenario evidence | Added scope/current-status pointer; original results retained |
| [docs/validation/DOCUMENTATION_RECONCILIATION_20261006.md](../../docs/validation/DOCUMENTATION_RECONCILIATION_20261006.md) | Current audit | New inventory and check record |
| [docs/validation/FORK_IMPROVEMENTS_HANDOFF_20261004.md](../../docs/validation/FORK_IMPROVEMENTS_HANDOFF_20261004.md) | Historical / scenario evidence | Added scope/current-status pointer; original results retained |
| [docs/validation/FORK_IMPROVEMENTS_PACKAGE_B_SOFTWARE_20261004.md](../../docs/validation/FORK_IMPROVEMENTS_PACKAGE_B_SOFTWARE_20261004.md) | Historical / scenario evidence | Added scope/current-status pointer; original results retained |
| [docs/validation/FORK_IMPROVEMENTS_PACKAGE_D_SOFTWARE_20261004.md](../../docs/validation/FORK_IMPROVEMENTS_PACKAGE_D_SOFTWARE_20261004.md) | Historical / scenario evidence | Added scope/current-status pointer; original results retained |
| [docs/validation/FORK_IMPROVEMENTS_PACKAGE_E_SOFTWARE_20261004.md](../../docs/validation/FORK_IMPROVEMENTS_PACKAGE_E_SOFTWARE_20261004.md) | Historical / scenario evidence | Added scope/current-status pointer; original results retained |
| [docs/validation/FORK_IMPROVEMENTS_PACKAGE_F_SOFTWARE_20261004.md](../../docs/validation/FORK_IMPROVEMENTS_PACKAGE_F_SOFTWARE_20261004.md) | Historical / scenario evidence | Added scope/current-status pointer; original results retained |
| [docs/validation/FORK_IMPROVEMENTS_PACKAGE_G_SOFTWARE_20261004.md](../../docs/validation/FORK_IMPROVEMENTS_PACKAGE_G_SOFTWARE_20261004.md) | Historical / scenario evidence | Added scope/current-status pointer; original results retained |
| [docs/validation/FORK_IMPROVEMENTS_PACKAGE_H_PROGRESS_20261004.md](../../docs/validation/FORK_IMPROVEMENTS_PACKAGE_H_PROGRESS_20261004.md) | Historical / scenario evidence | Added scope/current-status pointer; original results retained |
| [docs/validation/FORK_IMPROVEMENTS_PACKAGE_H_SOFTWARE_20261004.md](../../docs/validation/FORK_IMPROVEMENTS_PACKAGE_H_SOFTWARE_20261004.md) | Historical / scenario evidence | Added scope/current-status pointer; original results retained |
| [docs/validation/FORK_IMPROVEMENTS_PACKAGE_I_PROGRESS_20261004.md](../../docs/validation/FORK_IMPROVEMENTS_PACKAGE_I_PROGRESS_20261004.md) | Historical / scenario evidence | Added scope/current-status pointer; original results retained |
| [docs/validation/FORK_IMPROVEMENTS_PACKAGE_I_SOFTWARE_20261005.md](../../docs/validation/FORK_IMPROVEMENTS_PACKAGE_I_SOFTWARE_20261005.md) | Historical / scenario evidence | Added scope/current-status pointer; original results retained |
| [docs/validation/FORK_IMPROVEMENTS_PACKAGE_J_SOFTWARE_20261005.md](../../docs/validation/FORK_IMPROVEMENTS_PACKAGE_J_SOFTWARE_20261005.md) | Historical / scenario evidence | Added scope/current-status pointer; original results retained |
| [docs/validation/FORK_IMPROVEMENTS_PACKAGE_K_SOFTWARE_20261005.md](../../docs/validation/FORK_IMPROVEMENTS_PACKAGE_K_SOFTWARE_20261005.md) | Historical / scenario evidence | Added scope/current-status pointer; original results retained |
| [docs/validation/FORK_IMPROVEMENTS_PACKAGE_L_SOFTWARE_20261005.md](../../docs/validation/FORK_IMPROVEMENTS_PACKAGE_L_SOFTWARE_20261005.md) | Historical / scenario evidence | Added scope/current-status pointer; original results retained |
| [docs/validation/IMPLEMENTATION_PLAN_M2_2_REVIEW_20260923.md](../../docs/validation/IMPLEMENTATION_PLAN_M2_2_REVIEW_20260923.md) | Historical / scenario evidence | Added scope/current-status pointer; original results retained |
| [docs/validation/IMPLEMENTATION_REPORT_M2_2_REVIEW_20260923.md](../../docs/validation/IMPLEMENTATION_REPORT_M2_2_REVIEW_20260923.md) | Historical / scenario evidence | Added scope/current-status pointer; original results retained |
| [docs/validation/JC4880_H_CANDIDATE_20261004.md](../../docs/validation/JC4880_H_CANDIDATE_20261004.md) | Historical / scenario evidence | Added scope/current-status pointer; original results retained |
| [docs/validation/JC4880_L_CANDIDATE_20261005.md](../../docs/validation/JC4880_L_CANDIDATE_20261005.md) | Chronological hardware ledger | Added scope/current-status pointer; original results retained |
| [docs/validation/JC4880_V91_RELEASE_20261005.md](../../docs/validation/JC4880_V91_RELEASE_20261005.md) | Current release evidence | Frozen identity/results retained |
| [docs/validation/JC4880_V91_RELEASE_REVIEW_20261005.md](../../docs/validation/JC4880_V91_RELEASE_REVIEW_20261005.md) | Current release evidence | Frozen identity/results retained |
| [docs/validation/M2_1_PRODUCTION_RELEASE_20260920.md](../../docs/validation/M2_1_PRODUCTION_RELEASE_20260920.md) | Historical / scenario evidence | Added scope/current-status pointer; original results retained |
| [docs/validation/M2_2_PRODUCTION_RELEASE_20260923.md](../../docs/validation/M2_2_PRODUCTION_RELEASE_20260923.md) | Historical / scenario evidence | Added scope/current-status pointer; original results retained |
| [docs/validation/M2_3_RELEASE_DECISION_20260928.md](../../docs/validation/M2_3_RELEASE_DECISION_20260928.md) | Historical / scenario evidence | Added scope/current-status pointer; original results retained |
| [docs/validation/M2_4_PRODUCTION_RELEASE_20260929.md](../../docs/validation/M2_4_PRODUCTION_RELEASE_20260929.md) | Historical / scenario evidence | Added scope/current-status pointer; original results retained |
| [docs/validation/M2_AUTOMATED_RELEASE_GATE_20260920.md](../../docs/validation/M2_AUTOMATED_RELEASE_GATE_20260920.md) | Historical / scenario evidence | Added scope/current-status pointer; original results retained |
| [docs/validation/M2_POST_MERGE_20260920.md](../../docs/validation/M2_POST_MERGE_20260920.md) | Historical / scenario evidence | Added scope/current-status pointer; original results retained |
| [docs/validation/P4_BOUNDED_MEDIA_CACHE_20260919.md](../../docs/validation/P4_BOUNDED_MEDIA_CACHE_20260919.md) | Historical / scenario evidence | Added scope/current-status pointer; original results retained |
| [docs/validation/P4_DUAL_MASTER_TEMPO_WDT_20260919.md](../../docs/validation/P4_DUAL_MASTER_TEMPO_WDT_20260919.md) | Historical / scenario evidence | Added scope/current-status pointer; original results retained |
| [docs/validation/P4_DUAL_USB_LIFECYCLE_MATRIX_20260911.md](../../docs/validation/P4_DUAL_USB_LIFECYCLE_MATRIX_20260911.md) | Historical / scenario evidence | Added scope/current-status pointer; original results retained |
| [docs/validation/P4_DUPLICATE_TRACK_ID_ACCEPTANCE_20260928.md](../../docs/validation/P4_DUPLICATE_TRACK_ID_ACCEPTANCE_20260928.md) | Historical / scenario evidence | Added scope/current-status pointer; original results retained |
| [docs/validation/P4_DUPLICATE_TRACK_ID_ACCEPTANCE_PROCEDURE.md](../../docs/validation/P4_DUPLICATE_TRACK_ID_ACCEPTANCE_PROCEDURE.md) | Historical / scenario evidence | Added scope/current-status pointer; original results retained |
| [docs/validation/P4_FINAL_COMBINED_SOAK_20260920.md](../../docs/validation/P4_FINAL_COMBINED_SOAK_20260920.md) | Historical / scenario evidence | Added scope/current-status pointer; original results retained |
| [docs/validation/P4_M2_5_CC7_SCALING_20261002.md](../../docs/validation/P4_M2_5_CC7_SCALING_20261002.md) | Historical / scenario evidence | Added scope/current-status pointer; original results retained |
| [docs/validation/P4_M2_5_USB_REBOOT_INVESTIGATION_20261001.md](../../docs/validation/P4_M2_5_USB_REBOOT_INVESTIGATION_20261001.md) | Historical / scenario evidence | Added scope/current-status pointer; original results retained |
| [docs/validation/P4_PCM_TIMELINE_SCHEDULER_PROBE_20260927.md](../../docs/validation/P4_PCM_TIMELINE_SCHEDULER_PROBE_20260927.md) | Historical / scenario evidence | Added scope/current-status pointer; original results retained |
| [docs/validation/P4_POST_REVIEW_RELEASE_QUALIFICATION_20260925.md](../../docs/validation/P4_POST_REVIEW_RELEASE_QUALIFICATION_20260925.md) | Historical / scenario evidence | Added scope/current-status pointer; original results retained |
| [docs/validation/P4_POWER_VBUS_ACCEPTANCE_20260911.md](../../docs/validation/P4_POWER_VBUS_ACCEPTANCE_20260911.md) | Historical / scenario evidence | Added scope/current-status pointer; original results retained |
| [docs/validation/P4_PULL_OTA_FAULT_MATRIX_20260920.md](../../docs/validation/P4_PULL_OTA_FAULT_MATRIX_20260920.md) | Historical / scenario evidence | Added scope/current-status pointer; original results retained |
| [docs/validation/P4_SERVICE_NETWORK_RETRY_20260924.md](../../docs/validation/P4_SERVICE_NETWORK_RETRY_20260924.md) | Historical / scenario evidence | Added scope/current-status pointer; original results retained |
| [docs/validation/P4_UAC_IDLE_CONTINUITY_REMEDIATION_20260920.md](../../docs/validation/P4_UAC_IDLE_CONTINUITY_REMEDIATION_20260920.md) | Historical / scenario evidence | Added scope/current-status pointer; original results retained |
| [docs/validation/P4_USB_EXFAT_GPT_SMOKE.md](../../docs/validation/P4_USB_EXFAT_GPT_SMOKE.md) | Historical / scenario evidence | Added scope/current-status pointer; original results retained |
| [firmware/common/dj_link_core/README.md](../../firmware/common/dj_link_core/README.md) | Module/provenance | Attribution/license and software-only scope retained |
| [firmware/common/djlink/README.md](../../firmware/common/djlink/README.md) | Module/provenance | Attribution/license and software-only scope retained |
| [firmware/common/djlink/UPSTREAM.md](../../firmware/common/djlink/UPSTREAM.md) | Module/provenance | Attribution/license and software-only scope retained |
| [firmware/main-deck-jc1060/README.md](../../firmware/main-deck-jc1060/README.md) | Current operation/development | Reconciled or retained where already accurate |
| [firmware/main-deck-p4/PINOUT_P4.md](../../firmware/main-deck-p4/PINOUT_P4.md) | Current operation/development | Reconciled or retained where already accurate |
| [firmware/main-deck-p4/components/fatfs/README-Pajoniiir.md](https://github.com/dvucinozd/Pajoniiir/blob/9af99cd234e775521f3ec83c0104f5c6a0c72920/firmware/main-deck-p4/components/fatfs/README-Pajoniiir.md) | Current operation/development | Reconciled or retained where already accurate |
| [firmware/main-deck-p4/components/ui/DJ_UI_NOTICE.md](https://github.com/dvucinozd/Pajoniiir/blob/9af99cd234e775521f3ec83c0104f5c6a0c72920/firmware/main-deck-p4/components/ui/DJ_UI_NOTICE.md) | Module/provenance | Attribution/license and software-only scope retained |
| [firmware/main-deck-p4/components/web_server/web/index.html](https://github.com/dvucinozd/Pajoniiir/blob/9af99cd234e775521f3ec83c0104f5c6a0c72920/firmware/main-deck-p4/components/web_server/web/index.html) | Embedded runtime asset | Reviewed for boundary; unchanged firmware |
| [firmware/p4-dual-usb-spike/README.md](../../firmware/p4-dual-usb-spike/README.md) | Test/engineering guide | Current role checked; focused guide corrections where needed |
| [firmware/p4-only-software-harness/README.md](../../firmware/p4-only-software-harness/README.md) | Test/engineering guide | Current role checked; focused guide corrections where needed |
| [tests/anlz/README.md](../../tests/anlz/README.md) | Test/engineering guide | Current role checked; focused guide corrections where needed |
| [tests/api_contract/retired/README.md](../../tests/api_contract/retired/README.md) | Test/engineering guide | Current role checked; focused guide corrections where needed |
| [tests/audio_keylock_soak/README.md](../../tests/audio_keylock_soak/README.md) | Test/engineering guide | Current role checked; focused guide corrections where needed |
| [tests/support/README.md](../../tests/support/README.md) | Test/engineering guide | Current role checked; focused guide corrections where needed |
| [tests/ui_simulator/README.md](../../tests/ui_simulator/README.md) | Test/engineering guide | Current role checked; focused guide corrections where needed |
