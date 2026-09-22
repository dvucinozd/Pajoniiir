# Pajoniiir documentation

Status: **current P4-only documentation index, reconciled 2026-09-20**.

Pajoniiir M2.1 is a released standalone dual-deck system. The ESP32-P4 owns
USB media, DDJ-FLX4 control/audio, playback, mixer/DSP, display, network
services and signed OTA. There is no active S3 firmware or inter-board link.

## User documentation

- [`index.html`](index.html) — published M2.1 user manual.
- [`PROJECT_OVERVIEW.md`](PROJECT_OVERVIEW.md) — concise product and release
  overview.
- [`HARDWARE_WIRING.md`](HARDWARE_WIRING.md) — power, USB and PCM5102A wiring.
- [`STARTUP_CHECKLIST.md`](STARTUP_CHECKLIST.md) — operation, maintenance and
  release checklist.
- [`OTA-UPDATE.md`](OTA-UPDATE.md) — signed local/pull OTA and wired recovery.

## Developer documentation

- [`DOCUMENTATION_STATUS.md`](DOCUMENTATION_STATUS.md) — current production
  identity, accepted limitations and source-of-truth order.
- [`ARCHITECTURE.md`](ARCHITECTURE.md) — P4 runtime architecture and ownership.
- [`DEVELOPMENT_PLAN.md`](DEVELOPMENT_PLAN.md) — post-M2.1 maintenance roadmap.
- [`RISK_REGISTER.md`](RISK_REGISTER.md) — active and accepted product risks.
- [`SECURITY_PROVISIONING_POLICY.md`](SECURITY_PROVISIONING_POLICY.md) — M2.1
  network, signing-key and irreversible-provisioning decisions.
- [`DDJ_FLX4_MIDI_MAP.md`](DDJ_FLX4_MIDI_MAP.md) — FLX4 mapping and per-control
  acceptance status.
- [`CONTROLLER_PROFILE_SCHEMA.md`](CONTROLLER_PROFILE_SCHEMA.md) and
  [`CONTROLLER_PROFILE_UPDATE.md`](CONTROLLER_PROFILE_UPDATE.md) — P4-local
  controller profile format and update procedure.
- [`HERCULES_INPULSE_500_MIDI_MAP.md`](HERCULES_INPULSE_500_MIDI_MAP.md) —
  host-qualified, not hardware-qualified non-FLX4 profile.
- [`rekordbox-format-analysis.md`](rekordbox-format-analysis.md) — Rekordbox
  media/metadata reference.
- [`LIBAPTA_P4_INTEGRATION_PLAN.md`](LIBAPTA_P4_INTEGRATION_PLAN.md) — deferred
  optional integration, not current firmware capability.
- [`../firmware/main-deck-p4/PINOUT_P4.md`](../firmware/main-deck-p4/PINOUT_P4.md)
  — active P4/PCM5102A pin inventory.

Before committing documentation or media changes, run:

```powershell
.\tools\check_documentation.ps1
git diff --check
```

The `Documentation integrity` workflow repeats those checks for Markdown/HTML
links, local assets, references to removed documentation and changed-file
whitespace. Changes to `docs/` also retain the separate Pages deployment gate.

## M2.1 release evidence

- [`validation/M2_1_PRODUCTION_RELEASE_20260920.md`](validation/M2_1_PRODUCTION_RELEASE_20260920.md)
  — exact tagged build, installation, smoke and publication record.
- [`validation/M2_AUTOMATED_RELEASE_GATE_20260920.md`](validation/M2_AUTOMATED_RELEASE_GATE_20260920.md)
  and [`validation/M2_POST_MERGE_20260920.md`](validation/M2_POST_MERGE_20260920.md)
  — beta qualification and merge/CI provenance inherited by M2.1.
- [`validation/P4_DUAL_USB_LIFECYCLE_MATRIX_20260911.md`](validation/P4_DUAL_USB_LIFECYCLE_MATRIX_20260911.md)
  — complete USB lifecycle accounting.
- [`validation/P4_POWER_VBUS_ACCEPTANCE_20260911.md`](validation/P4_POWER_VBUS_ACCEPTANCE_20260911.md)
  — accepted unchanged power/VBUS wiring.
- [`validation/P4_BOUNDED_MEDIA_CACHE_20260919.md`](validation/P4_BOUNDED_MEDIA_CACHE_20260919.md)
  and [`validation/P4_USB_EXFAT_GPT_SMOKE.md`](validation/P4_USB_EXFAT_GPT_SMOKE.md)
  — media, filesystem and partition-layout evidence.
- [`validation/P4_DUAL_MASTER_TEMPO_WDT_20260919.md`](validation/P4_DUAL_MASTER_TEMPO_WDT_20260919.md),
  [`validation/P4_UAC_IDLE_CONTINUITY_REMEDIATION_20260920.md`](validation/P4_UAC_IDLE_CONTINUITY_REMEDIATION_20260920.md)
  and [`validation/P4_FINAL_COMBINED_SOAK_20260920.md`](validation/P4_FINAL_COMBINED_SOAK_20260920.md)
  — final audio remediation and soak evidence.
- [`validation/P4_PULL_OTA_FAULT_MATRIX_20260920.md`](validation/P4_PULL_OTA_FAULT_MATRIX_20260920.md)
  — signed pull/push OTA recovery evidence.

Superseded S3/dual-processor documents, completed implementation plans and
intermediate RC validation notes were removed from the working tree during the
M2.1 documentation reconciliation. They remain available in Git history when
historical investigation is required.
