/* SPDX-License-Identifier: Apache-2.0 */
/* Selected only for CONFIG_PAJONIIIR_P4_LOCAL_CONTROLLER. */
#include "controller_usb_host.h"
#include "controller_midi_out_gate.h"

#include "esp_log.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "usb/usb_host.h"

static const char *ROUTED_TAG = "controller_route";
static controller_midi_out_gate_t s_output_gate;

static esp_err_t routed_interface_claim(usb_host_client_handle_t client,
                                        usb_device_handle_t device,
                                        uint8_t interface_num,
                                        uint8_t alternate_setting)
{
    usb_device_info_t info = {0};
    const esp_err_t info_rc = usb_host_device_info(device, &info);
    if (info_rc != ESP_OK) {
        return info_rc;
    }
    if (info.parent.dev_hdl != NULL || info.parent.port_num != 1u) {
        ESP_LOGW(ROUTED_TAG,
                 "reject MIDI controller outside USB1 direct root: port=%u direct=%u",
                 (unsigned)info.parent.port_num,
                 info.parent.dev_hdl == NULL ? 1u : 0u);
        return ESP_ERR_NOT_SUPPORTED;
    }
    return usb_host_interface_claim(client, device, interface_num,
                                    alternate_setting);
}

static BaseType_t routed_queue_reset(QueueHandle_t queue)
{
    controller_midi_out_gate_stop(&s_output_gate);
    while (controller_midi_out_gate_active_producers(&s_output_gate) != 0u) {
        taskYIELD();
    }
    return xQueueGenericReset(queue, pdFALSE);
}

#define usb_host_interface_claim routed_interface_claim
#ifdef xQueueReset
#undef xQueueReset
#endif
#define xQueueReset routed_queue_reset
#define controller_usb_host_send_packet controller_usb_host_send_packet_unsafe
#include "controller_usb_host.c"
#undef controller_usb_host_send_packet
#undef xQueueReset
#undef usb_host_interface_claim

esp_err_t controller_usb_host_send_packet(const uint8_t packet[4])
{
    uint32_t generation = 0u;
    if (!controller_midi_out_gate_begin(&s_output_gate, &generation)) {
        return packet ? ESP_ERR_INVALID_STATE : ESP_ERR_INVALID_ARG;
    }
    const esp_err_t rc = controller_usb_host_send_packet_unsafe(packet);
    controller_midi_out_gate_end(&s_output_gate);

    /* A stop/start cannot complete while this producer is counted, but keep the
     * generation check as an explicit invariant and diagnostic guard. */
    if (generation != controller_midi_out_gate_generation(&s_output_gate)) {
        ESP_LOGE(ROUTED_TAG, "MIDI OUT generation changed during producer call");
        return ESP_ERR_INVALID_STATE;
    }
    return rc;
}

void controller_usb_host_output_gate_set_connected(bool connected)
{
    if (connected) {
        controller_midi_out_gate_start(&s_output_gate);
    } else {
        controller_midi_out_gate_stop(&s_output_gate);
    }
}
