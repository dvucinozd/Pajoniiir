/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Experimental production adapter selected only by
 * CONFIG_PAJONIIIR_P4_LOCAL_CONTROLLER.
 *
 * The storage implementation below remains byte-for-byte the mature product
 * owner. These three narrow adapters move Host Library ownership to the shared
 * dual-controller manager and restrict storage recovery to USB0.
 */
#include "usb_host_manager.h"

#include "esp_intr_alloc.h"
#include "freertos/task.h"
#include "usb/usb_host.h"

static esp_err_t shared_storage_host_install(const usb_host_config_t *ignored)
{
    (void)ignored;
    const usb_host_manager_config_t config = {
        .peripheral_map = USB_HOST_MANAGER_PERIPHERAL_MAP_DUAL,
        .root_port_unpowered = true,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
        .daemon_stack_size = 4096u,
        .daemon_priority = 4u,
        .daemon_core_id = tskNO_AFFINITY,
    };
    return usb_host_manager_init(&config);
}

static esp_err_t shared_storage_handle_events(TickType_t timeout,
                                              uint32_t *event_flags)
{
    /* usb_host_manager owns usb_host_lib_handle_events(). The legacy storage
     * task retains its recovery cadence but no longer competes for daemon
     * events. */
    if (event_flags) {
        *event_flags = 0u;
    }
    if (timeout == 0u) {
        taskYIELD();
    } else if (timeout == portMAX_DELAY) {
        vTaskDelay(pdMS_TO_TICKS(500));
    } else {
        vTaskDelay(timeout);
    }
    return ESP_OK;
}

static esp_err_t shared_storage_set_root_power(bool enable)
{
    return usb_host_manager_set_root_power_by_index(0u, enable);
}

#define usb_host_install shared_storage_host_install
#define usb_host_lib_handle_events shared_storage_handle_events
#define usb_host_lib_set_root_port_power shared_storage_set_root_power
#include "usb_storage.c"
