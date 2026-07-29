# Pajoniiir Documentation

Complete documentation index, audited 2026-07-28. Start with
[`DOCUMENTATION_STATUS.md`](DOCUMENTATION_STATUS.md) to understand current
scope, source-of-truth precedence, the installed baseline and the latest fully
accepted hardware baseline.

## Current product documents

- [`DOCUMENTATION_STATUS.md`](DOCUMENTATION_STATUS.md) — audit baseline, source-of-truth order and remaining scope.
- [`PROJECT_OVERVIEW.md`](PROJECT_OVERVIEW.md) — product shape and verified port status.
- [`ARCHITECTURE.md`](ARCHITECTURE.md) — FLX4/S3/P4 ownership and data flow.
- [`CONTROL_LINK_PROTOCOL.md`](CONTROL_LINK_PROTOCOL.md) — fixed `0xA5` events and `0xA6` bulk/profile/status transport.
- [`CONTROLLER_PROFILE_SCHEMA.md`](CONTROLLER_PROFILE_SCHEMA.md) — JSON and compiled S3 profile format.
- [`DDJ_FLX4_MIDI_MAP.md`](DDJ_FLX4_MIDI_MAP.md) — MIDI addresses, semantics and hardware-acceptance ledger.
- [`HARDWARE_WIRING.md`](HARDWARE_WIRING.md) — UART, I2S, USB and audio wiring.
- [`DEVELOPMENT_PLAN.md`](DEVELOPMENT_PLAN.md) — completed phases and remaining work.
- [`POST_R5_PLAN.md`](POST_R5_PLAN.md) — ordered current-candidate acceptance and enclosure-hardening plan.
- [`STARTUP_CHECKLIST.md`](STARTUP_CHECKLIST.md) — bring-up history and recurring pre-enclosure checks.
- [`RISK_REGISTER.md`](RISK_REGISTER.md) — open, monitored and accepted risks.
- [`R5_DEAD_CODE_AUDIT.md`](R5_DEAD_CODE_AUDIT.md) — completed R5 legacy-path cleanup evidence.
- [`VINYL_SCRATCH_PLAN.md`](VINYL_SCRATCH_PLAN.md) — scratch design, remediation and hardware acceptance.
- [`OTA-UPDATE.md`](OTA-UPDATE.md) — operator procedure for P4 and S3 OTA updates.
- [`OTA_UPDATE_PLAN.md`](OTA_UPDATE_PLAN.md) — OTA design, rollout batches and rollback evidence.
- [`CONTROLLER_PROFILE_UPDATE.md`](CONTROLLER_PROFILE_UPDATE.md) — safe SD profile upload through the P4 web UI/API.
- [`S3_WIFI_DEBUG_LOG.md`](S3_WIFI_DEBUG_LOG.md) — runtime S3 service AP/log viewer and OTA service.
- [`bench-notes.md`](bench-notes.md) — dated hardware bench observations.
- [`fixevi-remediation-audit.md`](fixevi-remediation-audit.md) — `fix/release-blockers-and-concurrency` remediation closeout.
- [`migration/ESP_IDF_6_0_2_MIGRATION.md`](migration/ESP_IDF_6_0_2_MIGRATION.md) — ESP-IDF 6.0.2 migration status and validation matrix.

## Analysis and decision records

These explain earlier constraints and choices. Their status headers identify
whether they remain active or are historical context.

- [`BLE_MIDI_FLX4_FEASIBILITY.md`](BLE_MIDI_FLX4_FEASIBILITY.md) — why BLE-MIDI FLX4 support is not planned.
- [`board-jc4880p443c-i-w-analysis.md`](board-jc4880p443c-i-w-analysis.md) — target P4 board analysis.
- [`control-board-decision.md`](control-board-decision.md) — historical origin and current disposition of the two-board/S3 decision.
- [`framework-decision.md`](framework-decision.md) — ESP-IDF production framework decision.
- [`rekordbox-format-analysis.md`](rekordbox-format-analysis.md) — PDB/ANLZ/media layout analysis.

## Validation records

- [`validation/FLX4_SMART_INPUT_CAPTURE.md`](validation/FLX4_SMART_INPUT_CAPTURE.md)
- [`validation/FLX4_LED_MIDI_OUT_CAPTURE.md`](validation/FLX4_LED_MIDI_OUT_CAPTURE.md)
- [`validation/FLX4_OFFICIAL_MIDI_GAP_SMOKE.md`](validation/FLX4_OFFICIAL_MIDI_GAP_SMOKE.md)
- [`validation/FLX4_USB_AUDIO_DESCRIPTOR_CAPTURE.md`](validation/FLX4_USB_AUDIO_DESCRIPTOR_CAPTURE.md)
- [`validation/FLX4_USB_AUDIO_E2E_SMOKE.md`](validation/FLX4_USB_AUDIO_E2E_SMOKE.md)
- [`validation/P4_USB_EXFAT_GPT_SMOKE.md`](validation/P4_USB_EXFAT_GPT_SMOKE.md)
- [`validation/P4_USB_RECOVERY_SMOKE.md`](validation/P4_USB_RECOVERY_SMOKE.md)
- [`validation/P4_OVERVIEW_DSI_SYNC_SMOKE_20260717.md`](validation/P4_OVERVIEW_DSI_SYNC_SMOKE_20260717.md)
- [`validation/SIGNED_OTA_RC1_131_DEPLOYMENT.md`](validation/SIGNED_OTA_RC1_131_DEPLOYMENT.md)

Validation files are dated evidence. A `PENDING` row remains pending unless a
newer active document explicitly records acceptance; do not infer a pass from
feature implementation alone.

## Historical design specifications

- [`superpowers/specs/2026-05-26-ui-polish-beat-jump-design.md`](superpowers/specs/2026-05-26-ui-polish-beat-jump-design.md)
- [`superpowers/specs/2026-05-26-ui-polish-empty-loading-error-states-design.md`](superpowers/specs/2026-05-26-ui-polish-empty-loading-error-states-design.md)
- [`superpowers/specs/2026-05-26-ui-polish-footer-tabs-design.md`](superpowers/specs/2026-05-26-ui-polish-footer-tabs-design.md)
- [`superpowers/specs/2026-05-26-ui-polish-hot-cues-loop-design.md`](superpowers/specs/2026-05-26-ui-polish-hot-cues-loop-design.md)
- [`superpowers/specs/2026-05-26-ui-polish-key-shift-design.md`](superpowers/specs/2026-05-26-ui-polish-key-shift-design.md)
- [`superpowers/specs/2026-05-26-ui-polish-overview-library-design.md`](superpowers/specs/2026-05-26-ui-polish-overview-library-design.md)
- [`superpowers/specs/2026-05-26-ui-polish-settings-design.md`](superpowers/specs/2026-05-26-ui-polish-settings-design.md)
- [`superpowers/specs/2026-06-08-ddj-flx4-bootstrap-design.md`](superpowers/specs/2026-06-08-ddj-flx4-bootstrap-design.md)
- [`superpowers/specs/2026-06-12-p4-ui-architecture-refactor-design.md`](superpowers/specs/2026-06-12-p4-ui-architecture-refactor-design.md)
- [`superpowers/specs/2026-06-22-flx4-beat-jump-behavior-design.md`](superpowers/specs/2026-06-22-flx4-beat-jump-behavior-design.md)
- [`superpowers/specs/2026-06-22-flx4-beat-loop-behavior-design.md`](superpowers/specs/2026-06-22-flx4-beat-loop-behavior-design.md)
- [`superpowers/specs/2026-06-22-flx4-shifted-beat-loop-leds-design.md`](superpowers/specs/2026-06-22-flx4-shifted-beat-loop-leds-design.md)
- [`superpowers/specs/2026-07-02-flx4-input-state-snapshot-design.md`](superpowers/specs/2026-07-02-flx4-input-state-snapshot-design.md)
- [`superpowers/specs/2026-07-02-flx4-jog-search-master-cue-design.md`](superpowers/specs/2026-07-02-flx4-jog-search-master-cue-design.md)
- [`superpowers/specs/2026-07-03-p4-usb-exfat-gpt-design.md`](superpowers/specs/2026-07-03-p4-usb-exfat-gpt-design.md)
- [`superpowers/specs/2026-07-07-s3-debug-ap-log-viewer-design.md`](superpowers/specs/2026-07-07-s3-debug-ap-log-viewer-design.md)

These files preserve design intent. Several UI concepts were later removed or
replaced: the current touch tabs are Overview, Library, Hot Cues and Settings;
Loop, Beat Jump and Key Shift remain controller behavior rather than dedicated
touch screens. Current vinyl behavior is documented in `VINYL_SCRATCH_PLAN.md`.

## Reference material

- [`reference/DDJ-FLX4_MIDI_message_List.md`](reference/DDJ-FLX4_MIDI_message_List.md) — searchable transcription/reference notes.
- [`reference/DDJ-FLX4_MIDI_message_List_E1.pdf`](reference/DDJ-FLX4_MIDI_message_List_E1.pdf) — vendor PDF.
- [`reference/Pioneer-DDJ-FLX4.midi.xml`](reference/Pioneer-DDJ-FLX4.midi.xml) — Mixxx mapping used as the authoritative MIDI address seed.

Reference files are not runtime specifications. Mixxx script bindings do not
define standalone P4 behavior.

## Images

- [`images/p4.jpg`](images/p4.jpg) — target board photo.
- [`images/overview.jpg`](images/overview.jpg) — representative Overview capture.
- [`images/library.jpg`](images/library.jpg) — representative Library capture.
- [`images/settings.jpg`](images/settings.jpg) — representative Settings capture.

The active Hot Cues tab currently has no archived screenshot. UI images are
illustrative and may lag small firmware polish changes.

## Documentation outside `docs/`

- [`../README.md`](../README.md) — repository entry point.
- [`../AGENTS.md`](../AGENTS.md) — contribution and verification rules.
- [`../firmware/control-board-s3/CLAUDE.md`](../firmware/control-board-s3/CLAUDE.md) — S3 developer guide.
- [`../firmware/control-board-s3/PINOUT.md`](../firmware/control-board-s3/PINOUT.md) — legacy/board pinout context.
- [`../firmware/control-board-s3/PINOUT_XIAO_ESP32S3.md`](../firmware/control-board-s3/PINOUT_XIAO_ESP32S3.md) — active XIAO wiring.
- [`../firmware/main-deck-p4/CLAUDE.md`](../firmware/main-deck-p4/CLAUDE.md) — P4 developer guide.
- [`../firmware/main-deck-p4/PINOUT_P4.md`](../firmware/main-deck-p4/PINOUT_P4.md) — P4 pin assignments.
- [`../firmware/main-deck-p4/components/fatfs/README-Pajoniiir.md`](../firmware/main-deck-p4/components/fatfs/README-Pajoniiir.md) — local FATFS integration notes.
- [`../tests/anlz/README.md`](../tests/anlz/README.md) — ANLZ host-test fixture guide.

## Local inputs not committed

- `../upstream/esp32_p4_jc4880p433c_bsp/`
- `../JC4880P443C_I_W/`

These paths are optional analyst inputs and must not be assumed present on
another developer's machine.
