/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "usb_midi_codec.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CONTROLLER_USB_PRODUCT_MAX 64u

typedef struct {
    uint16_t vid;
    uint16_t pid;
    uint8_t address;
    uint8_t speed;
    uint8_t parent_port;
    bool direct_root_child;
    usb_midi_endpoints_t midi;
    char product[CONTROLLER_USB_PRODUCT_MAX];
} controller_usb_identity_t;

typedef void (*controller_usb_midi_cb_t)(const usb_midi_message_t *message,
                                         void *ctx);
typedef void (*controller_usb_connection_cb_t)(
    bool connected, const controller_usb_identity_t *identity, void *ctx);

typedef struct {
    controller_usb_midi_cb_t midi_cb;
    controller_usb_connection_cb_t connection_cb;
    void *callback_ctx;
    uint32_t task_stack_size;
    UBaseType_t task_priority;
    BaseType_t task_core_id;
    UBaseType_t midi_out_queue_depth;
    int max_event_messages;
} controller_usb_host_config_t;

typedef struct {
    uint32_t devices_probed;
    uint32_t descriptor_rejects;
    uint32_t midi_connects;
    uint32_t midi_disconnects;
    uint32_t midi_packets;
    uint32_t midi_bytes;
    uint32_t midi_parse_rejects;
    uint32_t midi_in_submit_failures;
    uint32_t midi_out_submit_failures;
    uint32_t midi_out_queue_drops;
    uint32_t probe_event_drops;
    uint32_t recovery_requests;
    bool registered;
    bool connected;
    bool accepting_midi_out;
} controller_usb_host_diagnostics_t;

esp_err_t controller_usb_host_init(const controller_usb_host_config_t *config);
esp_err_t controller_usb_host_send_packet(const uint8_t packet[4]);
bool controller_usb_host_is_connected(void);
bool controller_usb_host_get_identity(controller_usb_identity_t *identity_out);
void controller_usb_host_get_diagnostics(
    controller_usb_host_diagnostics_t *diag_out);

#ifdef __cplusplus
}
#endif
