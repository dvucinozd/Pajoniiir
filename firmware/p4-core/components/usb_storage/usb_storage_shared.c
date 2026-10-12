/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Experimental production adapter selected only by
 * the P4-only dual-USB runtime.
 *
 * The storage implementation below remains byte-for-byte the mature product
 * owner. These three narrow adapters move Host Library ownership to the shared
 * dual-controller manager and restrict storage recovery to USB0.
 */
#include "usb_host_manager.h"
#include "board_capabilities.h"

#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "freertos/task.h"
#include "usb/usb_host.h"

#define SHARED_STORAGE_ROOT_INDEX (board_capabilities_get()->storage_root)

static bool shared_storage_device_route_allowed(uint8_t address)
{
    bool matches = false;
    /* The USB stack reports parent.dev_hdl == NULL even behind a hub, so the
     * root of a hub child cannot be resolved. With a hub on the storage
     * connector the root is not checked; the device must still be observed. */
    const bool hub = board_capabilities_get()->storage_via_hub;
    const esp_err_t rc = usb_host_manager_device_matches_root(
        address, SHARED_STORAGE_ROOT_INDEX, !hub, &matches);
    if (hub && rc == ESP_OK) {
        return true;
    }
    if (rc == ESP_ERR_NOT_FOUND) {
        /* The topology client and MSC client receive the same enumeration
         * edge independently. The storage owner retries mounting, so defer
         * until the topology client has published this address. */
        return false;
    }
    return rc == ESP_OK && matches;
}

static bool shared_storage_request_root_recovery(const char *why)
{
    (void)why;
#if CONFIG_USB_HOST_DIAGNOSTIC_HS_ONLY
    /* This diagnostic has no storage root. Keep the mature owner's cadence
     * from queuing recovery against the deliberately absent FS controller. */
    return true;
#else
    const esp_err_t rc = usb_host_manager_request_recovery(
        SHARED_STORAGE_ROOT_INDEX,
        USB_HOST_RECOVERY_REASON_ENUMERATION);
    if (rc != ESP_OK) {
        ESP_LOGW("usb_storage", "USB0 recovery request failed: %s",
                 esp_err_to_name(rc));
    }
    /* Never fall through to the legacy direct power cycle in the dual-host
     * image. A later storage cadence will retry a dropped request. */
    return true;
#endif
}

static esp_err_t shared_storage_host_install(const usb_host_config_t *ignored)
{
    (void)ignored;
    const usb_host_manager_config_t config = {
#if CONFIG_USB_HOST_DIAGNOSTIC_HS_ONLY
        .peripheral_map = 1u << 0,
#else
        .peripheral_map = USB_HOST_MANAGER_PERIPHERAL_MAP_DUAL,
#endif
        .root_port_unpowered = true,
        /* JC4880P443 routes its Full-Speed Type-C connector to the P4 PHY0
         * pair (GPIO24/GPIO25, USB1P1_N/P). The ESP32-P4 reset mapping sends
         * USB-OTG FS to PHY1 (GPIO26/GPIO27), which is not connected to that
         * receptacle. Select PHY0 in software; do not burn USB_PHY_SEL. */
#if CONFIG_USB_HOST_DIAGNOSTIC_HS_ONLY
        .override_fs_phy_index = false,
#else
        .override_fs_phy_index = true,
#endif
        .fs_phy_index = board_capabilities_get()->fs_phy_index,
#if CONFIG_PAJONIIIR_BOARD_M3 && CONFIG_CONTROLLER_USB_HS_AUDIO_DIAGNOSTIC
        /* HS periodic OUT must rearm before the next 125-us microframe.
         * Allow it to preempt level-1 peripheral handlers on the display core.
         * This does not bypass critical sections; qualify on the exact image. */
        .intr_flags = ESP_INTR_FLAG_LEVEL3,
#else
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
#endif
        .daemon_stack_size = 4096u,
        .daemon_priority = 4u,
#if CONFIG_CONTROLLER_USB_HS_AUDIO_DIAGNOSTIC
        /* usb_host_install allocates both host interrupts on the calling core.
         * Keep the HS diagnostic scheduler away from core-0 audio/decode
         * critical sections; an unpinned installer makes IRQ placement depend
         * on startup scheduling. Production topology remains unchanged. */
        .daemon_core_id = 1,
#else
        .daemon_core_id = tskNO_AFFINITY,
#endif
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
    return usb_host_manager_set_root_power_by_index(
        SHARED_STORAGE_ROOT_INDEX, enable);
}

#define usb_host_install shared_storage_host_install
#define usb_host_lib_handle_events shared_storage_handle_events
#define usb_host_lib_set_root_port_power shared_storage_set_root_power
#define USB_STORAGE_DEVICE_ROUTE_ALLOWED(address) \
    shared_storage_device_route_allowed(address)
#define USB_STORAGE_REQUEST_ROOT_RECOVERY(why) \
    shared_storage_request_root_recovery(why)
#include "usb_storage.c"
