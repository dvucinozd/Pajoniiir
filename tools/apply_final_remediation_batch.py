#!/usr/bin/env python3
"""Finish the release-audit remediation in one serial, idempotent batch."""

from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BRANCH = "fix/release-blockers-and-concurrency"


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def write(rel: str, text: str) -> None:
    (ROOT / rel).write_text(text, encoding="utf-8")


def replace_once(rel: str, old: str, new: str, label: str) -> None:
    text = read(rel)
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match in {rel}, found {count}")
    write(rel, text.replace(old, new, 1))


def ensure_replace(rel: str, marker: str, old: str, new: str, label: str) -> None:
    text = read(rel)
    if marker in text:
        print(f"skip {label}: marker present")
        return
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match in {rel}, found {count}")
    write(rel, text.replace(old, new, 1))


def run_script(rel: str) -> None:
    subprocess.run(["python3", str(ROOT / rel)], cwd=ROOT, check=True)


# 1. Apply the two source ownership transformations that were prepared but had
# not yet landed when the Actions queue was paused.
profile_rel = "firmware/main-deck-p4/components/controller_profile_manager/controller_profile_manager.c"
if "s_descriptor_q" not in read(profile_rel):
    run_script("tools/apply_profile_manager_sd_ownership.py")
else:
    print("skip profile ownership: already applied")

# The descriptor worker is inserted before the locked matcher definition.
ensure_replace(
    profile_rel,
    "static int cpm_on_descriptor_locked(uint16_t vid, uint16_t pid);",
    "static int cpm_process_descriptor_report(uint16_t vid, uint16_t pid,\n",
    "static int cpm_on_descriptor_locked(uint16_t vid, uint16_t pid);\n\n"
    "static int cpm_process_descriptor_report(uint16_t vid, uint16_t pid,\n",
    "profile matcher forward declaration",
)

profile_cmake = "firmware/main-deck-p4/components/controller_profile_manager/CMakeLists.txt"
ensure_replace(
    profile_cmake,
    "service_log sd_io_gate",
    "REQUIRES control_link freertos service_log)",
    "REQUIRES control_link freertos service_log sd_io_gate)",
    "profile manager SD gate dependency",
)

deck_rel = "firmware/main-deck-p4/components/deck_core/deck_core.c"
if "DECK_CORE_INTERNAL_RESET_ID" not in read(deck_rel):
    run_script("tools/apply_deck_actor_snapshot.py")
else:
    print("skip deck actor snapshot: already applied")

# Clean up the reset semaphore on partial init failure as well.
ensure_replace(
    deck_rel,
    "vSemaphoreDelete(s_reset_done_sem)",
    "    if (s_mutex) {\n        vSemaphoreDelete(s_mutex);\n        s_mutex = NULL;\n    }\n",
    "    if (s_reset_done_sem) {\n        vSemaphoreDelete(s_reset_done_sem);\n        s_reset_done_sem = NULL;\n    }\n"
    "    if (s_mutex) {\n        vSemaphoreDelete(s_mutex);\n        s_mutex = NULL;\n    }\n",
    "deck reset semaphore cleanup",
)

# 2. Resolve the build/test blockers found in run 177.
settings_rel = "firmware/main-deck-p4/components/app_settings/app_settings_fixed.c"
replace_once(
    settings_rel,
    '#include "freertos/task.h"\n',
    '#include "freertos/FreeRTOS.h"\n#include "freertos/task.h"\n',
    "FreeRTOS include order",
)
settings_cmake = "firmware/main-deck-p4/components/app_settings/CMakeLists.txt"
ensure_replace(
    settings_cmake,
    "REQUIRES log nvs_flash freertos",
    "REQUIRES log nvs_flash",
    "REQUIRES log nvs_flash freertos",
    "settings FreeRTOS dependency",
)

midi_rel = "firmware/control-board-s3/components/flx4_midi_host/flx4_midi_host.c"
midi = read(midi_rel)
if "static usb_host_client_handle_t s_midi_client_handle;\n\nstatic void publish_connection_refresh_from_usb_owner" not in midi:
    midi = midi.replace(
        "static void publish_connection_refresh_from_usb_owner(void)\n",
        "static usb_host_client_handle_t s_midi_client_handle;\n\n"
        "static void publish_connection_refresh_from_usb_owner(void)\n",
        1,
    )
    # Remove the later duplicate declaration from the host-state block.
    later = "static QueueHandle_t s_midi_out_queue = NULL;\nstatic usb_host_client_handle_t s_midi_client_handle;\n"
    if later not in midi:
        raise RuntimeError("MIDI client handle late declaration anchor missing")
    midi = midi.replace(later, "static QueueHandle_t s_midi_out_queue = NULL;\n", 1)
    write(midi_rel, midi)

host_runner = "tests/run_p4_host_tests.ps1"
old_gate = '''Assert-FileContains `
    -Name "p4 priority touch supersedes stale edges and survives button-only saturation" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/control_link/control_link_uart.c") `
    -LiteralPatterns @("event_is_jog_touch(&cur) && cur.id == ev->id", "queued older edge must never execute after the latest level", "button-only saturation", "xQueueSendToFront(s_event_queue, ev, portMAX_DELAY)")
'''
new_gate = '''Assert-FileContains `
    -Name "p4 control queue preserves edges and coalesces only continuous values" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/control_link/control_link_uart.c") `
    -LiteralPatterns @("store_pending_continuous", "Button/state edges are lossless", "xQueueSend(s_event_queue, ev, portMAX_DELAY)")

Assert-FileDoesNotContain `
    -Name "p4 UART producer never drains or reorders the deck queue" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/control_link/control_link_uart.c") `
    -LiteralPatterns @("xQueueReceive(s_event_queue")
'''
ensure_replace(
    host_runner,
    "p4 control queue preserves edges and coalesces only continuous values",
    old_gate,
    new_gate,
    "control queue host gate",
)

# 3. Central ownership for all AP -> STA -> AP transitions.
wifi_rel = "firmware/main-deck-p4/components/wifi_link/wifi_link.c"
ensure_replace(
    wifi_rel,
    '#include "wifi_transition_lease.h"',
    '#include "wifi_link_retry.h"\n',
    '#include "wifi_link_retry.h"\n#include "wifi_transition_lease.h"\n',
    "Wi-Fi lease include",
)
ensure_replace(
    wifi_rel,
    "wifi_transition_lease_release(WIFI_TRANSITION_OWNER_PROBE);",
    "    s_probe_running = false;\n    vTaskDelete(NULL);\n",
    "    s_probe_running = false;\n"
    "    wifi_transition_lease_release(WIFI_TRANSITION_OWNER_PROBE);\n"
    "    vTaskDelete(NULL);\n",
    "probe lease release",
)
ensure_replace(
    wifi_rel,
    "wifi_transition_lease_acquire(WIFI_TRANSITION_OWNER_PROBE)",
    "    if (ssid[0] == '\\0') return ESP_ERR_INVALID_ARG;\n\n    s_probe_running = true;\n",
    "    if (ssid[0] == '\\0') return ESP_ERR_INVALID_ARG;\n\n"
    "    esp_err_t lease_rc = wifi_transition_lease_acquire(WIFI_TRANSITION_OWNER_PROBE);\n"
    "    if (lease_rc != ESP_OK) return lease_rc;\n\n"
    "    s_probe_running = true;\n",
    "probe lease acquire",
)
ensure_replace(
    wifi_rel,
    "wifi_transition_lease_release(WIFI_TRANSITION_OWNER_PROBE);\n        probe_note(WIFI_LINK_PROBE_FAILED",
    "        s_probe_running = false;\n        probe_note(WIFI_LINK_PROBE_FAILED, ESP_ERR_NO_MEM, \"could not start task\");\n",
    "        s_probe_running = false;\n"
    "        wifi_transition_lease_release(WIFI_TRANSITION_OWNER_PROBE);\n"
    "        probe_note(WIFI_LINK_PROBE_FAILED, ESP_ERR_NO_MEM, \"could not start task\");\n",
    "probe task-create lease rollback",
)

wifi_cmake = "firmware/main-deck-p4/components/wifi_link/CMakeLists.txt"
ensure_replace(
    wifi_cmake,
    "service_log wifi_transition_lease",
    "web_server service_log",
    "web_server service_log wifi_transition_lease",
    "wifi link lease dependency",
)

ota_rel = "firmware/main-deck-p4/components/p4_ota_pull/p4_ota_pull.c"
ensure_replace(
    ota_rel,
    '#include "wifi_transition_lease.h"',
    '#include "wifi_link.h"\n',
    '#include "wifi_link.h"\n#include "wifi_transition_lease.h"\n',
    "OTA lease include",
)
ensure_replace(
    ota_rel,
    "wifi_transition_lease_release(WIFI_TRANSITION_OWNER_OTA);\n    vTaskDelete(NULL);",
    "    s_running = false;\n    vTaskDelete(NULL);\n",
    "    s_running = false;\n"
    "    wifi_transition_lease_release(WIFI_TRANSITION_OWNER_OTA);\n"
    "    vTaskDelete(NULL);\n",
    "OTA check lease release",
)
# install_task has a reboot branch after s_running=false.
ensure_replace(
    ota_rel,
    "wifi_transition_lease_release(WIFI_TRANSITION_OWNER_OTA);\n    if (rc == ESP_OK)",
    "    s_running = false;\n    if (rc == ESP_OK) {\n",
    "    s_running = false;\n"
    "    wifi_transition_lease_release(WIFI_TRANSITION_OWNER_OTA);\n"
    "    if (rc == ESP_OK) {\n",
    "OTA install lease release",
)
# Acquire only after all request validation has passed.
ota = read(ota_rel)
install_anchor = "    if (s_install_url[0] == '\\0' || s_status.available_size == 0u) {\n        return ESP_ERR_INVALID_STATE;\n    }\n\n    s_running = true;\n"
if "wifi_transition_lease_acquire(WIFI_TRANSITION_OWNER_OTA)" not in ota:
    if install_anchor not in ota:
        raise RuntimeError("OTA install lease anchor missing")
    ota = ota.replace(
        install_anchor,
        "    if (s_install_url[0] == '\\0' || s_status.available_size == 0u) {\n"
        "        return ESP_ERR_INVALID_STATE;\n"
        "    }\n\n"
        "    esp_err_t lease_rc = wifi_transition_lease_acquire(WIFI_TRANSITION_OWNER_OTA);\n"
        "    if (lease_rc != ESP_OK) return lease_rc;\n\n"
        "    s_running = true;\n",
        1,
    )
    check_anchor = "    if (p4_ota_cfg_check_url(url) != P4_OTA_CFG_OK) return ESP_ERR_INVALID_ARG;\n\n    s_running = true;\n"
    if check_anchor not in ota:
        raise RuntimeError("OTA check lease anchor missing")
    ota = ota.replace(
        check_anchor,
        "    if (p4_ota_cfg_check_url(url) != P4_OTA_CFG_OK) return ESP_ERR_INVALID_ARG;\n\n"
        "    esp_err_t lease_rc = wifi_transition_lease_acquire(WIFI_TRANSITION_OWNER_OTA);\n"
        "    if (lease_rc != ESP_OK) return lease_rc;\n\n"
        "    s_running = true;\n",
        1,
    )
    # Both task creation failures must return the lease.
    failure = "        s_running = false;\n        note(P4_OTA_PULL_FAILED, ESP_ERR_NO_MEM, \"could not start task\");\n"
    if ota.count(failure) != 2:
        raise RuntimeError(f"OTA task-create failure anchors: {ota.count(failure)}")
    ota = ota.replace(
        failure,
        "        s_running = false;\n"
        "        wifi_transition_lease_release(WIFI_TRANSITION_OWNER_OTA);\n"
        "        note(P4_OTA_PULL_FAILED, ESP_ERR_NO_MEM, \"could not start task\");\n",
    )
    write(ota_rel, ota)

ota_cmake = "firmware/main-deck-p4/components/p4_ota_pull/CMakeLists.txt"
ensure_replace(
    ota_cmake,
    "wifi_link wifi_transition_lease",
    "app_settings wifi_link esp_http_client",
    "app_settings wifi_link wifi_transition_lease esp_http_client",
    "OTA lease dependency",
)

# 4. Restore the single permanent CI after this batch is validated. The commit
# produced by the batch workflow will trigger exactly one clean validation run.
ci_rel = ".github/workflows/esp-idf-6-migration.yml"
ci = read(ci_rel)
manual = "on:\n  workflow_dispatch:\n"
automatic = '''on:
  push:
    branches:
      - migration/esp-idf-6.0.2
      - fix/release-blockers-and-concurrency
  pull_request:
    paths:
      - "firmware/**"
      - "tests/**"
      - ".github/workflows/esp-idf-6-migration.yml"
  workflow_dispatch:
'''
if manual not in ci:
    raise RuntimeError("manual CI trigger block missing")
write(ci_rel, ci.replace(manual, automatic, 1))

# 5. Remove every one-shot transformation workflow/script. Production history
# keeps the commits; the branch keeps only source, tests and the permanent CI.
transient = [
    ".github/workflows/apply-deck-actor-snapshot.yml",
    ".github/workflows/apply-profile-manager-sd-ownership.yml",
    ".github/workflows/apply-release-audit-pending.yml",
    ".github/workflows/apply-release-audit-phase1.yml",
    ".github/workflows/apply-release-audit-phase2-s3-midi.yml",
    ".github/workflows/apply-release-audit-phase3-s3-debug-ap.yml",
    ".github/workflows/apply-release-audit-phase4-s3-audio.yml",
    ".github/workflows/apply-release-audit-phase5-anlz.yml",
    ".github/workflows/apply-release-audit-phase6-audio-lifecycle.yml",
    ".github/workflows/apply-release-audit-phase7-control-queue.yml",
    ".github/workflows/apply-release-audit-phase8-parser-settings.yml",
    ".github/workflows/apply-release-audit-phase9-loops.yml",
    ".github/workflows/apply-final-remediation-batch.yml",
    ".github/remediation-batch.trigger",
    "tools/apply_deck_actor_snapshot.py",
    "tools/apply_profile_manager_sd_ownership.py",
    "tools/apply_release_audit_phase1.py",
    "tools/apply_release_audit_phase2_s3_midi.py",
    "tools/apply_release_audit_phase3_s3_debug_ap.py",
    "tools/apply_release_audit_phase4_s3_audio.py",
    "tools/apply_release_audit_phase5_anlz_ownership.py",
    "tools/apply_release_audit_phase6_audio_lifecycle.py",
    "tools/apply_release_audit_phase7_control_queue.py",
    "tools/apply_release_audit_phase8_parser_settings.py",
    "tools/apply_release_audit_phase9_authoritative_loops.py",
    "tools/run_pending_release_audit.py",
    "tools/apply_final_remediation_batch.py",
]
for rel in transient:
    path = ROOT / rel
    if path.exists():
        path.unlink()
        print(f"removed transient {rel}")

print("Final remediation batch prepared.")
