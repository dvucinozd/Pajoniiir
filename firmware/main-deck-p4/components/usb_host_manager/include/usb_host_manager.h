/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Single owner for the ESP-IDF USB Host Library on ESP32-P4.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "usb/usb_host.h"
#include "usb_host_recovery_arbiter.h"

#ifdef __cplusplus
extern "C" {
#endif

#define USB_HOST_MANAGER_PERIPHERAL_MAP_DUAL ((1u << 0) | (1u << 1))

typedef struct {
    unsigned peripheral_map;
    bool root_port_unpowered;
    int intr_flags;
    uint32_t daemon_stack_size;
    UBaseType_t daemon_priority;
    BaseType_t daemon_core_id;
} usb_host_manager_config_t;

typedef struct {
    esp_err_t install_result;
    uint32_t daemon_iterations;
    uint32_t daemon_errors;
    uint32_t no_clients_events;
    uint32_t all_free_events;
    uint32_t root_power_requested_mask;
    uint32_t topology_observations;
    uint32_t topology_probe_failures;
    uint32_t recovery_queue_drops;
    uint32_t recovery_requests;
    uint32_t recovery_coalesced_requests;
    uint32_t recovery_successes;
    uint32_t recovery_failures;
    unsigned peripheral_map;
    bool ready;
    bool root_power_requested;
} usb_host_manager_diagnostics_t;

esp_err_t usb_host_manager_init(const usb_host_manager_config_t *config);
bool usb_host_manager_is_ready(void);
esp_err_t usb_host_manager_set_all_root_power(bool enable);
esp_err_t usb_host_manager_set_root_power_by_index(uint8_t root_port_index,
                                                   bool enable);
esp_err_t usb_host_manager_device_matches_root(uint8_t address,
                                               uint8_t root_port_index,
                                               bool require_direct_root,
                                               bool *matches_out);
esp_err_t usb_host_manager_request_recovery(
    uint8_t root_port_index,
    usb_host_recovery_reason_t reason);
esp_err_t usb_host_manager_get_library_info(usb_host_lib_info_t *info_out);
void usb_host_manager_get_diagnostics(usb_host_manager_diagnostics_t *diag_out);

#ifdef __cplusplus
}
#endif
