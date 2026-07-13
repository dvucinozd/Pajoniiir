#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "control_link.h"
#include "ui_settings.h"

static void test_force_poll_always_allows_refresh(void)
{
    assert(ui_settings_should_poll(1000, 999, true, 1000));
}

static void test_first_poll_and_interval_gate(void)
{
    assert(ui_settings_should_poll(1000, 0, false, 1000));
    assert(!ui_settings_should_poll(1500, 1000, false, 1000));
    assert(ui_settings_should_poll(2000, 1000, false, 1000));
}

static void test_master_trim_presets_are_non_boosting_and_cycle(void)
{
    assert(ui_settings_master_trim_preset_count() == 3);
    assert(ui_settings_master_trim_next_preset(0) == 1);
    assert(ui_settings_master_trim_next_preset(1) == 2);
    assert(ui_settings_master_trim_next_preset(2) == 0);
    assert(ui_settings_master_trim_next_preset(99) == 0);
    assert(ui_settings_master_trim_sanitize_preset(0) == 0);
    assert(ui_settings_master_trim_sanitize_preset(1) == 1);
    assert(ui_settings_master_trim_sanitize_preset(2) == 2);
    assert(ui_settings_master_trim_sanitize_preset(3) == 0);
    assert(ui_settings_master_trim_sanitize_preset(255) == 0);

    assert(ui_settings_master_trim_gain(0) > 0.999f);
    assert(ui_settings_master_trim_gain(0) <= 1.0f);
    assert(ui_settings_master_trim_gain(1) > 0.70f);
    assert(ui_settings_master_trim_gain(1) < 0.72f);
    assert(ui_settings_master_trim_gain(2) > 0.50f);
    assert(ui_settings_master_trim_gain(2) < 0.51f);
    assert(ui_settings_master_trim_gain(99) == ui_settings_master_trim_gain(0));

    assert(ui_settings_master_trim_label(0) && ui_settings_master_trim_label(0)[0] != '\0');
    assert(ui_settings_master_trim_label(1) && ui_settings_master_trim_label(1)[0] != '\0');
    assert(ui_settings_master_trim_label(2) && ui_settings_master_trim_label(2)[0] != '\0');
    assert(ui_settings_master_trim_label(99) == ui_settings_master_trim_label(0));
}

static void test_s3_debug_ap_status_labels_are_stable(void)
{
    assert(strcmp(ui_settings_s3_debug_ap_status_label(CTRL_S3_DEBUG_AP_OFF), "OFF") == 0);
    assert(strcmp(ui_settings_s3_debug_ap_status_label(CTRL_S3_DEBUG_AP_STARTING), "STARTING") == 0);
    assert(strcmp(ui_settings_s3_debug_ap_status_label(CTRL_S3_DEBUG_AP_ON), "ON") == 0);
    assert(strcmp(ui_settings_s3_debug_ap_status_label(CTRL_S3_DEBUG_AP_ERROR), "ERROR") == 0);
    assert(strcmp(ui_settings_s3_debug_ap_status_label(99), "UNKNOWN") == 0);
}

static void test_settings_active_tab_uses_configured_index(void)
{
    assert(ui_settings_is_active_tab(5, 5));
    assert(!ui_settings_is_active_tab(6, 5));
    assert(!ui_settings_is_active_tab(5, -1));
}

static void test_firmware_status_labels_are_stable(void)
{
    assert(strcmp(ui_settings_firmware_slot_label(CTRL_FW_SLOT_OTA_0), "ota_0") == 0);
    assert(strcmp(ui_settings_firmware_slot_label(CTRL_FW_SLOT_OTA_1), "ota_1") == 0);
    assert(strcmp(ui_settings_firmware_slot_label(CTRL_FW_SLOT_FACTORY), "factory") == 0);
    assert(strcmp(ui_settings_firmware_slot_label(99), "unknown") == 0);
    assert(strcmp(ui_settings_firmware_state_label(CTRL_FW_STATE_PENDING_VERIFY), "PENDING") == 0);
    assert(strcmp(ui_settings_firmware_state_label(CTRL_FW_STATE_VALID), "VALID") == 0);
    assert(strcmp(ui_settings_firmware_state_label(CTRL_FW_STATE_INVALID), "INVALID") == 0);
    assert(strcmp(ui_settings_firmware_state_label(99), "UNKNOWN") == 0);
}

int main(void)
{
    test_force_poll_always_allows_refresh();
    test_first_poll_and_interval_gate();
    test_master_trim_presets_are_non_boosting_and_cycle();
    test_s3_debug_ap_status_labels_are_stable();
    test_settings_active_tab_uses_configured_index();
    test_firmware_status_labels_are_stable();

    puts("ui_settings tests passed");
    return 0;
}
