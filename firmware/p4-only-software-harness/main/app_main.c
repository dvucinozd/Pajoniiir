/* SPDX-License-Identifier: Apache-2.0 */
#include <inttypes.h>

#include "controller_runtime.h"
#include "controller_usb_host.h"
#include "esp_err.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "usb/msc_host.h"
#include "usb_host_manager.h"

static const char *TAG = "p4_only_harness";
static uint32_t s_msc_connects;
static uint32_t s_msc_disconnects;
static uint32_t s_semantic_events;

static void msc_event_callback(const msc_host_event_t *event, void *arg)
{
    (void)arg;
    if (!event) {
        return;
    }
    if (event->event == MSC_DEVICE_CONNECTED) {
        (void)__atomic_add_fetch(&s_msc_connects, 1u, __ATOMIC_RELAXED);
        ESP_LOGI(TAG, "MSC event connected addr=%u",
                 (unsigned)event->device.address);
    } else if (event->event == MSC_DEVICE_DISCONNECTED) {
        (void)__atomic_add_fetch(&s_msc_disconnects, 1u, __ATOMIC_RELAXED);
        ESP_LOGI(TAG, "MSC event disconnected");
    }
}

static void semantic_event_callback(const flx4_control_event_t *event,
                                    void *ctx)
{
    (void)ctx;
    if (!event) {
        return;
    }
    const uint32_t count =
        __atomic_add_fetch(&s_semantic_events, 1u, __ATOMIC_RELAXED);
    if (count <= 24u || (count % 256u) == 0u) {
        ESP_LOGI(TAG,
                 "SEMANTIC #%" PRIu32 " type=0x%02X id=0x%02X value=%d",
                 count, event->type, event->id, event->value);
    }
}

static void midi_callback(const usb_midi_message_t *message, void *ctx)
{
    (void)ctx;
    if (message) {
        (void)controller_runtime_handle_midi(message);
    }
}

static void connection_callback(bool connected,
                                const controller_usb_identity_t *identity,
                                void *ctx)
{
    (void)ctx;
    controller_runtime_set_connected(connected);
    if (!connected || !identity) {
        ESP_LOGW(TAG, "controller disconnected");
        return;
    }
    ESP_LOGW(TAG,
             "controller connected VID=0x%04X PID=0x%04X product='%s' "
             "parent_port=%u direct_root=%u",
             identity->vid, identity->pid, identity->product,
             identity->parent_port, identity->direct_root_child ? 1u : 0u);
}

void app_main(void)
{
    ESP_LOGW(TAG,
             "P4-only software harness: shared host + MSC + MIDI + local map");

    const controller_runtime_config_t runtime_config = {
        .event_cb = semantic_event_callback,
        .callback_ctx = NULL,
    };
    ESP_ERROR_CHECK(controller_runtime_init(&runtime_config));

    const usb_host_manager_config_t host_config = {
        .peripheral_map = USB_HOST_MANAGER_PERIPHERAL_MAP_DUAL,
        .root_port_unpowered = true,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
        .daemon_stack_size = 4096u,
        .daemon_priority = 4u,
        .daemon_core_id = tskNO_AFFINITY,
    };
    ESP_ERROR_CHECK(usb_host_manager_init(&host_config));

    const msc_host_driver_config_t msc_config = {
        .create_backround_task = true,
        .task_priority = 5,
        .stack_size = 4096,
        .core_id = tskNO_AFFINITY,
        .callback = msc_event_callback,
        .callback_arg = NULL,
    };
    ESP_ERROR_CHECK(msc_host_install(&msc_config));

    const controller_usb_host_config_t controller_config = {
        .midi_cb = midi_callback,
        .connection_cb = connection_callback,
        .callback_ctx = NULL,
        .task_stack_size = 8192u,
        .task_priority = 5u,
        .task_core_id = tskNO_AFFINITY,
        .midi_out_queue_depth = 64u,
        .max_event_messages = 8,
    };
    ESP_ERROR_CHECK(controller_usb_host_init(&controller_config));

    const esp_err_t power_rc = usb_host_manager_set_all_root_power(true);
    if (power_rc != ESP_OK && power_rc != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(power_rc);
    }

    for (;;) {
        usb_host_manager_diagnostics_t host_diag;
        controller_usb_host_diagnostics_t controller_diag;
        controller_runtime_diagnostics_t runtime_diag;
        usb_host_lib_info_t library_info = {0};
        usb_host_manager_get_diagnostics(&host_diag);
        controller_usb_host_get_diagnostics(&controller_diag);
        controller_runtime_get_diagnostics(&runtime_diag);
        const esp_err_t info_rc =
            usb_host_manager_get_library_info(&library_info);

        ESP_LOGW(TAG,
                 "status host_ready=%u map=0x%02X daemon_errors=%" PRIu32
                 " devices=%d clients=%d MSC(conn=%" PRIu32
                 " disc=%" PRIu32 ") MIDI(connected=%u conn=%" PRIu32
                 " disc=%" PRIu32 " packets=%" PRIu32
                 " in_fail=%" PRIu32 " out_fail=%" PRIu32
                 " queue_drop=%" PRIu32 ") MAP(events=%" PRIu32
                 " unmapped=%" PRIu32 " snapshots=%" PRIu32 ")",
                 host_diag.ready ? 1u : 0u,
                 host_diag.peripheral_map,
                 host_diag.daemon_errors,
                 info_rc == ESP_OK ? library_info.num_devices : -1,
                 info_rc == ESP_OK ? library_info.num_clients : -1,
                 __atomic_load_n(&s_msc_connects, __ATOMIC_ACQUIRE),
                 __atomic_load_n(&s_msc_disconnects, __ATOMIC_ACQUIRE),
                 controller_diag.connected ? 1u : 0u,
                 controller_diag.midi_connects,
                 controller_diag.midi_disconnects,
                 controller_diag.midi_packets,
                 controller_diag.midi_in_submit_failures,
                 controller_diag.midi_out_submit_failures,
                 controller_diag.midi_out_queue_drops,
                 runtime_diag.semantic_events,
                 runtime_diag.unmapped_messages,
                 runtime_diag.reconnect_snapshots);
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
