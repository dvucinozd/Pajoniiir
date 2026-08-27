/* SPDX-License-Identifier: Apache-2.0 */
#include "controller_usb_host.h"

#include <string.h>

#include "esp_log.h"
#include "controller_usb_audio_stream.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "usb/usb_host.h"
#include "usb_host_manager.h"

static const char *TAG = "controller_usb";
#define DEFAULT_TRANSFER_BYTES 64
#define FLX4_USB_VID 0x2B73u
#define FLX4_USB_PID 0x0045u
#define CONTROLLER_USB_ACTIVE_PRIORITY 7u

typedef struct {
    uint32_t generation;
    uint8_t packet[4];
} controller_midi_out_item_t;

typedef struct {
    usb_host_client_handle_t client;
    usb_device_handle_t device;
    usb_transfer_t *in_transfer;
    usb_transfer_t *out_transfer;
    QueueHandle_t out_queue;
    QueueHandle_t probe_queue;
    controller_usb_host_config_t config;
    controller_usb_identity_t identity;
    bool opened;
    bool claimed;
    bool in_active;
    bool out_active;
    bool closing;
    bool device_gone;
    bool out_generation_closed;
} controller_state_t;

static controller_state_t s_state;
static esp_err_t s_register_result = ESP_ERR_INVALID_STATE;
static bool s_registered;
static bool s_connected;
static bool s_accepting_out;
static uint32_t s_devices_probed;
static uint32_t s_descriptor_rejects;
static uint32_t s_midi_descriptor_rejects;
static uint32_t s_interface_claim_failures;
static uint32_t s_transfer_alloc_failures;
static uint32_t s_midi_connects;
static uint32_t s_midi_disconnects;
static uint32_t s_midi_packets;
static uint32_t s_midi_bytes;
static uint32_t s_midi_parse_rejects;
static uint32_t s_midi_in_submit_failures;
static uint32_t s_midi_out_submit_failures;
static uint32_t s_midi_out_queue_drops;
static uint32_t s_probe_event_drops;
static uint32_t s_recovery_requests;
static int32_t s_last_probe_result = ESP_ERR_INVALID_STATE;
static uint16_t s_last_seen_vid;
static uint16_t s_last_seen_pid;
static uint16_t s_last_config_total_length;
static uint8_t s_last_probe_stage;
static uint8_t s_last_probe_address;
static uint8_t s_last_parent_port;
static bool s_last_direct_root;
static uint32_t s_out_generation = 1u;

static inline void count_inc(uint32_t *value)
{
    (void)__atomic_add_fetch(value, 1u, __ATOMIC_RELAXED);
}

static void record_probe_result(controller_usb_probe_stage_t stage,
                                esp_err_t result)
{
    __atomic_store_n(&s_last_probe_stage, (uint8_t)stage, __ATOMIC_RELEASE);
    __atomic_store_n(&s_last_probe_result, (int32_t)result,
                     __ATOMIC_RELEASE);
}

static void request_controller_recovery(void)
{
    const esp_err_t rc = usb_host_manager_request_recovery(
        1u, USB_HOST_RECOVERY_REASON_TRANSFER);
    if (rc == ESP_OK) {
        count_inc(&s_recovery_requests);
    }
}

static void usb_string_to_ascii(const usb_str_desc_t *desc, char *out,
                                size_t out_size)
{
    if (!out || out_size == 0u) {
        return;
    }
    out[0] = '\0';
    if (!desc || desc->bLength < 2u) {
        return;
    }
    size_t chars = (size_t)(desc->bLength - 2u) / 2u;
    if (chars >= out_size) {
        chars = out_size - 1u;
    }
    for (size_t i = 0u; i < chars; ++i) {
        const uint16_t code_unit = desc->wData[i];
        out[i] = code_unit >= 0x20u && code_unit <= 0x7Eu
                     ? (char)code_unit
                     : '?';
    }
    out[chars] = '\0';
}

static void publish_connection(bool connected)
{
    __atomic_store_n(&s_connected, connected, __ATOMIC_RELEASE);
    if (s_state.config.connection_cb) {
        s_state.config.connection_cb(connected,
                                     connected ? &s_state.identity : NULL,
                                     s_state.config.callback_ctx);
    }
}

static esp_err_t submit_in_if_idle(controller_state_t *state)
{
    if (!state->opened || !state->claimed || state->closing ||
        !state->in_transfer || state->in_active) {
        return ESP_OK;
    }
    state->in_transfer->device_handle = state->device;
    state->in_transfer->bEndpointAddress = state->identity.midi.in_ep_addr;
    state->in_transfer->num_bytes = usb_round_up_to_mps(
        DEFAULT_TRANSFER_BYTES, state->identity.midi.in_ep_mps);
    const esp_err_t rc = usb_host_transfer_submit(state->in_transfer);
    if (rc == ESP_OK) {
        state->in_active = true;
    } else {
        count_inc(&s_midi_in_submit_failures);
        request_controller_recovery();
    }
    return rc;
}

static esp_err_t submit_out_if_idle(controller_state_t *state)
{
    if (!state->opened || !state->claimed || state->closing ||
        !state->out_transfer || state->out_active || !state->out_queue) {
        return ESP_OK;
    }

    const size_t capacity = state->out_transfer->data_buffer_size / 4u;
    size_t packets = 0u;
    controller_midi_out_item_t item;
    while (packets < capacity &&
           xQueueReceive(state->out_queue, &item, 0) == pdTRUE) {
        const uint32_t current_generation =
            __atomic_load_n(&s_out_generation, __ATOMIC_ACQUIRE);
        if (item.generation != current_generation ||
            !__atomic_load_n(&s_accepting_out, __ATOMIC_ACQUIRE)) {
            continue;
        }
        memcpy(&state->out_transfer->data_buffer[packets * 4u],
               item.packet, sizeof(item.packet));
        packets++;
    }
    if (packets == 0u) {
        return ESP_OK;
    }

    state->out_transfer->device_handle = state->device;
    state->out_transfer->bEndpointAddress = state->identity.midi.out_ep_addr;
    state->out_transfer->num_bytes = (int)(packets * 4u);
    const esp_err_t rc = usb_host_transfer_submit(state->out_transfer);
    if (rc == ESP_OK) {
        state->out_active = true;
    } else {
        count_inc(&s_midi_out_submit_failures);
        request_controller_recovery();
    }
    return rc;
}

static void midi_in_callback(usb_transfer_t *transfer)
{
    controller_state_t *state = (controller_state_t *)transfer->context;
    state->in_active = false;

    const bool terminal =
        transfer->status == USB_TRANSFER_STATUS_NO_DEVICE ||
        transfer->status == USB_TRANSFER_STATUS_CANCELED;
    if (terminal) {
        vTaskPrioritySet(NULL, state->config.task_priority);
        state->closing = true;
        state->device_gone =
            state->device_gone ||
            transfer->status == USB_TRANSFER_STATUS_NO_DEVICE;
        __atomic_store_n(&s_accepting_out, false, __ATOMIC_RELEASE);
    }

    if (transfer->status == USB_TRANSFER_STATUS_COMPLETED) {
        (void)__atomic_add_fetch(&s_midi_bytes,
                                 (uint32_t)transfer->actual_num_bytes,
                                 __ATOMIC_RELAXED);
        for (int offset = 0; offset + 3 < transfer->actual_num_bytes;
             offset += 4) {
            usb_midi_message_t message;
            if (!usb_midi_parse_event_packet(&transfer->data_buffer[offset],
                                             &message)) {
                count_inc(&s_midi_parse_rejects);
                continue;
            }
            count_inc(&s_midi_packets);
            if (state->config.midi_cb) {
                state->config.midi_cb(&message,
                                      state->config.callback_ctx);
            }
        }
    } else if (transfer->status != USB_TRANSFER_STATUS_NO_DEVICE &&
               transfer->status != USB_TRANSFER_STATUS_CANCELED) {
        ESP_LOGW(TAG, "MIDI IN transfer status=%d", (int)transfer->status);
        request_controller_recovery();
    }

    if (!state->closing && !terminal) {
        const esp_err_t rc = submit_in_if_idle(state);
        if (rc != ESP_OK) {
            ESP_LOGW(TAG, "MIDI IN resubmit: %s", esp_err_to_name(rc));
        }
    }
}

static void midi_out_callback(usb_transfer_t *transfer)
{
    controller_state_t *state = (controller_state_t *)transfer->context;
    state->out_active = false;

    const bool terminal =
        transfer->status == USB_TRANSFER_STATUS_NO_DEVICE ||
        transfer->status == USB_TRANSFER_STATUS_CANCELED;
    if (terminal) {
        vTaskPrioritySet(NULL, state->config.task_priority);
        state->closing = true;
        state->device_gone =
            state->device_gone ||
            transfer->status == USB_TRANSFER_STATUS_NO_DEVICE;
        __atomic_store_n(&s_accepting_out, false, __ATOMIC_RELEASE);
    }

    if (transfer->status != USB_TRANSFER_STATUS_COMPLETED &&
        transfer->status != USB_TRANSFER_STATUS_NO_DEVICE &&
        transfer->status != USB_TRANSFER_STATUS_CANCELED) {
        ESP_LOGW(TAG, "MIDI OUT transfer status=%d", (int)transfer->status);
        request_controller_recovery();
    }
    if (!state->closing && !terminal) {
        (void)submit_out_if_idle(state);
    }
}

static void close_step(controller_state_t *state)
{
    state->closing = true;
    __atomic_store_n(&s_accepting_out, false, __ATOMIC_RELEASE);
    if (!state->out_generation_closed) {
        (void)__atomic_add_fetch(&s_out_generation, 1u, __ATOMIC_ACQ_REL);
        state->out_generation_closed = true;
    }
    controller_usb_audio_stream_request_stop(state->device_gone);
    if (!controller_usb_audio_stream_poll_cleanup()) {
        return;
    }
    if (state->out_queue) {
        (void)xQueueReset(state->out_queue);
    }
    if (state->in_active || state->out_active) {
        return;
    }
    if (state->in_transfer) {
        if (usb_host_transfer_free(state->in_transfer) != ESP_OK) {
            return;
        }
        state->in_transfer = NULL;
    }
    if (state->out_transfer) {
        if (usb_host_transfer_free(state->out_transfer) != ESP_OK) {
            return;
        }
        state->out_transfer = NULL;
    }
    if (state->claimed) {
        const esp_err_t rc = usb_host_interface_release(
            state->client, state->device,
            state->identity.midi.interface_num);
        if (rc != ESP_OK) {
            ESP_LOGW(TAG, "release MIDI interface: %s", esp_err_to_name(rc));
            return;
        }
        state->claimed = false;
    }
    if (state->opened) {
        const esp_err_t rc =
            usb_host_device_close(state->client, state->device);
        if (rc != ESP_OK) {
            ESP_LOGW(TAG, "close controller device: %s", esp_err_to_name(rc));
            return;
        }
        state->opened = false;
        state->device = NULL;
    }

    const bool was_connected =
        __atomic_exchange_n(&s_connected, false, __ATOMIC_ACQ_REL);
    memset(&state->identity, 0, sizeof(state->identity));
    state->closing = false;
    state->device_gone = false;
    state->out_generation_closed = false;
    if (was_connected) {
        count_inc(&s_midi_disconnects);
        if (state->config.connection_cb) {
            state->config.connection_cb(false, NULL,
                                        state->config.callback_ctx);
        }
        ESP_LOGI(TAG, "USB-MIDI controller disconnected");
    }
}

static esp_err_t probe_device(controller_state_t *state, uint8_t address)
{
    count_inc(&s_devices_probed);
    __atomic_store_n(&s_last_probe_address, address, __ATOMIC_RELEASE);
    record_probe_result(CONTROLLER_USB_PROBE_OPEN, ESP_ERR_INVALID_STATE);
    usb_device_handle_t device = NULL;
    esp_err_t rc = usb_host_device_open(state->client, address, &device);
    if (rc != ESP_OK) {
        record_probe_result(CONTROLLER_USB_PROBE_OPEN, rc);
        return rc;
    }

    usb_device_info_t info = {0};
    const usb_device_desc_t *device_desc = NULL;
    const usb_config_desc_t *config_desc = NULL;
    rc = usb_host_device_info(device, &info);
    if (rc != ESP_OK) {
        record_probe_result(CONTROLLER_USB_PROBE_DEVICE_INFO, rc);
    } else {
        __atomic_store_n(&s_last_parent_port, info.parent.port_num,
                         __ATOMIC_RELEASE);
        __atomic_store_n(&s_last_direct_root, info.parent.dev_hdl == NULL,
                         __ATOMIC_RELEASE);
        rc = usb_host_get_device_descriptor(device, &device_desc);
        if (rc != ESP_OK) {
            record_probe_result(CONTROLLER_USB_PROBE_DEVICE_DESCRIPTOR, rc);
        }
    }
    if (rc == ESP_OK) {
        __atomic_store_n(&s_last_seen_vid, device_desc->idVendor,
                         __ATOMIC_RELEASE);
        __atomic_store_n(&s_last_seen_pid, device_desc->idProduct,
                         __ATOMIC_RELEASE);
        rc = usb_host_get_active_config_descriptor(device, &config_desc);
        if (rc != ESP_OK) {
            record_probe_result(CONTROLLER_USB_PROBE_CONFIG_DESCRIPTOR, rc);
        }
    }
    if (rc != ESP_OK || !device_desc || !config_desc) {
        count_inc(&s_descriptor_rejects);
        if (rc == ESP_OK) {
            record_probe_result(CONTROLLER_USB_PROBE_CONFIG_DESCRIPTOR,
                                ESP_FAIL);
        }
        (void)usb_host_device_close(state->client, device);
        return rc == ESP_OK ? ESP_FAIL : rc;
    }
    __atomic_store_n(&s_last_config_total_length, config_desc->wTotalLength,
                     __ATOMIC_RELEASE);

    usb_midi_endpoints_t endpoints;
    if (!usb_midi_find_streaming_endpoints((const uint8_t *)config_desc,
                                           config_desc->wTotalLength,
                                           &endpoints)) {
        count_inc(&s_midi_descriptor_rejects);
        record_probe_result(CONTROLLER_USB_PROBE_MIDI_DESCRIPTOR,
                            ESP_ERR_NOT_FOUND);
        (void)usb_host_device_close(state->client, device);
        return ESP_ERR_NOT_FOUND;
    }
    if (state->opened || state->claimed) {
        record_probe_result(CONTROLLER_USB_PROBE_ALREADY_OWNED,
                            ESP_ERR_INVALID_STATE);
        (void)usb_host_device_close(state->client, device);
        return ESP_ERR_INVALID_STATE;
    }

    state->device = device;
    state->opened = true;
    state->closing = false;
    state->device_gone = false;
    state->identity = (controller_usb_identity_t) {
        .vid = device_desc->idVendor,
        .pid = device_desc->idProduct,
        .address = address,
        .speed = (uint8_t)info.speed,
        .parent_port = info.parent.port_num,
        .direct_root_child = info.parent.dev_hdl == NULL,
        .midi = endpoints,
    };
    usb_string_to_ascii(info.str_desc_product, state->identity.product,
                        sizeof(state->identity.product));

    rc = usb_host_interface_claim(state->client, state->device,
                                  endpoints.interface_num,
                                  endpoints.alternate_setting);
    if (rc != ESP_OK) {
        count_inc(&s_interface_claim_failures);
        record_probe_result(CONTROLLER_USB_PROBE_INTERFACE_CLAIM, rc);
        state->closing = true;
        close_step(state);
        return rc;
    }
    state->claimed = true;

    const int in_bytes = usb_round_up_to_mps(DEFAULT_TRANSFER_BYTES,
                                             endpoints.in_ep_mps);
    const int out_bytes = usb_round_up_to_mps(DEFAULT_TRANSFER_BYTES,
                                              endpoints.out_ep_mps);
    rc = usb_host_transfer_alloc(in_bytes, 0, &state->in_transfer);
    if (rc == ESP_OK) {
        rc = usb_host_transfer_alloc(out_bytes, 0, &state->out_transfer);
    }
    if (rc != ESP_OK) {
        count_inc(&s_transfer_alloc_failures);
        record_probe_result(CONTROLLER_USB_PROBE_TRANSFER_ALLOC, rc);
        state->closing = true;
        close_step(state);
        return rc;
    }

    state->in_transfer->device_handle = state->device;
    state->in_transfer->bEndpointAddress = endpoints.in_ep_addr;
    state->in_transfer->callback = midi_in_callback;
    state->in_transfer->context = state;
    state->out_transfer->device_handle = state->device;
    state->out_transfer->bEndpointAddress = endpoints.out_ep_addr;
    state->out_transfer->callback = midi_out_callback;
    state->out_transfer->context = state;

    rc = submit_in_if_idle(state);
    if (rc != ESP_OK) {
        record_probe_result(CONTROLLER_USB_PROBE_IN_SUBMIT, rc);
        state->closing = true;
        close_step(state);
        return rc;
    }

    if (state->identity.vid == FLX4_USB_VID &&
        state->identity.pid == FLX4_USB_PID &&
        state->identity.direct_root_child &&
        state->identity.parent_port == 1u) {
        const esp_err_t audio_rc = controller_usb_audio_stream_start(
            state->client, state->device, (const uint8_t *)config_desc,
            config_desc->wTotalLength, xTaskGetCurrentTaskHandle(),
            CONTROLLER_USB_ACTIVE_PRIORITY, state->config.task_priority);
        if (audio_rc != ESP_OK) {
            ESP_LOGW(TAG, "FLX4 UAC unavailable; MIDI remains active: %s",
                     esp_err_to_name(audio_rc));
        } else {
            state->identity.usb_audio_active = true;
        }
    }

    count_inc(&s_midi_connects);
    (void)__atomic_add_fetch(&s_out_generation, 1u, __ATOMIC_ACQ_REL);
    state->out_generation_closed = false;
    __atomic_store_n(&s_accepting_out, true, __ATOMIC_RELEASE);
    publish_connection(true);
    record_probe_result(CONTROLLER_USB_PROBE_READY, ESP_OK);
    ESP_LOGI(TAG,
             "USB-MIDI ready addr=%u VID=0x%04X PID=0x%04X intf=%u "
             "alt=%u IN=0x%02X/%u OUT=0x%02X/%u parent_port=%u direct_root=%u",
             address, state->identity.vid, state->identity.pid,
             endpoints.interface_num, endpoints.alternate_setting,
             endpoints.in_ep_addr, endpoints.in_ep_mps,
             endpoints.out_ep_addr, endpoints.out_ep_mps,
             state->identity.parent_port,
             state->identity.direct_root_child ? 1u : 0u);
    return ESP_OK;
}

static void client_event_callback(const usb_host_client_event_msg_t *event_msg,
                                  void *arg)
{
    controller_state_t *state = (controller_state_t *)arg;
    switch (event_msg->event) {
    case USB_HOST_CLIENT_EVENT_NEW_DEV:
        if (!state->probe_queue ||
            xQueueSend(state->probe_queue, &event_msg->new_dev.address, 0) !=
                pdTRUE) {
            count_inc(&s_probe_event_drops);
        }
        break;
    case USB_HOST_CLIENT_EVENT_DEV_GONE:
        if (state->opened && event_msg->dev_gone.dev_hdl == state->device) {
            state->closing = true;
            state->device_gone = true;
            __atomic_store_n(&s_accepting_out, false, __ATOMIC_RELEASE);
            controller_usb_audio_stream_request_stop(true);
        }
        break;
    default:
        break;
    }
}

static void controller_task(void *arg)
{
    TaskHandle_t starter = (TaskHandle_t)arg;
    const usb_host_client_config_t client_config = {
        .is_synchronous = false,
        .max_num_event_msg = s_state.config.max_event_messages,
        .flags = {
            .notify_dev_removed = 1u,
        },
        .async = {
            .client_event_callback = client_event_callback,
            .callback_arg = &s_state,
        },
    };

    s_register_result =
        usb_host_client_register(&client_config, &s_state.client);
    __atomic_store_n(&s_registered, s_register_result == ESP_OK,
                     __ATOMIC_RELEASE);
    xTaskNotifyGive(starter);
    if (s_register_result != ESP_OK) {
        vTaskDelete(NULL);
        return;
    }

    for (;;) {
        const esp_err_t rc = usb_host_client_handle_events(
            s_state.client, pdMS_TO_TICKS(100));
        if (rc != ESP_OK && rc != ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "client events: %s", esp_err_to_name(rc));
        }
        if (s_state.closing) {
            close_step(&s_state);
            continue;
        }
        (void)controller_usb_audio_stream_poll_cleanup();
        uint8_t address = 0u;
        while (!s_state.closing && s_state.probe_queue &&
               xQueueReceive(s_state.probe_queue, &address, 0) == pdTRUE) {
            const esp_err_t probe_rc = probe_device(&s_state, address);
            if (probe_rc != ESP_OK && probe_rc != ESP_ERR_NOT_FOUND &&
                probe_rc != ESP_ERR_INVALID_STATE) {
                ESP_LOGW(TAG, "probe addr=%u: %s", address,
                         esp_err_to_name(probe_rc));
            }
        }
        (void)submit_in_if_idle(&s_state);
        (void)submit_out_if_idle(&s_state);
    }
}

esp_err_t controller_usb_host_init(const controller_usb_host_config_t *config)
{
    if (!config || config->task_stack_size < 4096u ||
        config->task_priority == 0u || config->midi_out_queue_depth == 0u ||
        config->max_event_messages < 2) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!usb_host_manager_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (__atomic_load_n(&s_registered, __ATOMIC_ACQUIRE)) {
        return ESP_OK;
    }

    memset(&s_state, 0, sizeof(s_state));
    s_state.config = *config;
    s_state.out_queue = xQueueCreate(config->midi_out_queue_depth,
                                     sizeof(controller_midi_out_item_t));
    s_state.probe_queue = xQueueCreate((UBaseType_t)config->max_event_messages,
                                       sizeof(uint8_t));
    if (!s_state.out_queue || !s_state.probe_queue) {
        if (s_state.out_queue) {
            vQueueDelete(s_state.out_queue);
            s_state.out_queue = NULL;
        }
        if (s_state.probe_queue) {
            vQueueDelete(s_state.probe_queue);
            s_state.probe_queue = NULL;
        }
        return ESP_ERR_NO_MEM;
    }

    const TaskHandle_t starter = xTaskGetCurrentTaskHandle();
    BaseType_t created;
    if (config->task_core_id == tskNO_AFFINITY) {
        created = xTaskCreate(controller_task, "controller_usb",
                              config->task_stack_size, (void *)starter,
                              config->task_priority, NULL);
    } else {
        created = xTaskCreatePinnedToCore(
            controller_task, "controller_usb", config->task_stack_size,
            (void *)starter, config->task_priority, NULL,
            config->task_core_id);
    }
    if (created != pdPASS) {
        vQueueDelete(s_state.out_queue);
        s_state.out_queue = NULL;
        vQueueDelete(s_state.probe_queue);
        s_state.probe_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5000)) == 0u) {
        return ESP_ERR_TIMEOUT;
    }
    return s_register_result;
}

esp_err_t controller_usb_host_send_packet(const uint8_t packet[4])
{
    if (!packet) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!__atomic_load_n(&s_accepting_out, __ATOMIC_ACQUIRE) ||
        !s_state.out_queue) {
        return ESP_ERR_INVALID_STATE;
    }
    controller_midi_out_item_t item = {
        .generation = __atomic_load_n(&s_out_generation, __ATOMIC_ACQUIRE),
    };
    memcpy(item.packet, packet, sizeof(item.packet));
    if (xQueueSend(s_state.out_queue, &item, 0) != pdTRUE) {
        count_inc(&s_midi_out_queue_drops);
        return ESP_ERR_TIMEOUT;
    }
    if (s_state.client) {
        (void)usb_host_client_unblock(s_state.client);
    }
    return ESP_OK;
}

bool controller_usb_host_is_connected(void)
{
    return __atomic_load_n(&s_connected, __ATOMIC_ACQUIRE);
}

bool controller_usb_host_get_identity(controller_usb_identity_t *identity_out)
{
    if (!identity_out || !controller_usb_host_is_connected()) {
        return false;
    }
    *identity_out = s_state.identity;
    return true;
}

void controller_usb_host_get_diagnostics(
    controller_usb_host_diagnostics_t *diag_out)
{
    if (!diag_out) {
        return;
    }
    *diag_out = (controller_usb_host_diagnostics_t) {
        .devices_probed =
            __atomic_load_n(&s_devices_probed, __ATOMIC_ACQUIRE),
        .descriptor_rejects =
            __atomic_load_n(&s_descriptor_rejects, __ATOMIC_ACQUIRE),
        .midi_descriptor_rejects =
            __atomic_load_n(&s_midi_descriptor_rejects, __ATOMIC_ACQUIRE),
        .interface_claim_failures =
            __atomic_load_n(&s_interface_claim_failures, __ATOMIC_ACQUIRE),
        .transfer_alloc_failures =
            __atomic_load_n(&s_transfer_alloc_failures, __ATOMIC_ACQUIRE),
        .midi_connects =
            __atomic_load_n(&s_midi_connects, __ATOMIC_ACQUIRE),
        .midi_disconnects =
            __atomic_load_n(&s_midi_disconnects, __ATOMIC_ACQUIRE),
        .midi_packets =
            __atomic_load_n(&s_midi_packets, __ATOMIC_ACQUIRE),
        .midi_bytes =
            __atomic_load_n(&s_midi_bytes, __ATOMIC_ACQUIRE),
        .midi_parse_rejects =
            __atomic_load_n(&s_midi_parse_rejects, __ATOMIC_ACQUIRE),
        .midi_in_submit_failures =
            __atomic_load_n(&s_midi_in_submit_failures, __ATOMIC_ACQUIRE),
        .midi_out_submit_failures =
            __atomic_load_n(&s_midi_out_submit_failures, __ATOMIC_ACQUIRE),
        .midi_out_queue_drops =
            __atomic_load_n(&s_midi_out_queue_drops, __ATOMIC_ACQUIRE),
        .probe_event_drops =
            __atomic_load_n(&s_probe_event_drops, __ATOMIC_ACQUIRE),
        .recovery_requests =
            __atomic_load_n(&s_recovery_requests, __ATOMIC_ACQUIRE),
        .last_probe_result =
            __atomic_load_n(&s_last_probe_result, __ATOMIC_ACQUIRE),
        .last_seen_vid =
            __atomic_load_n(&s_last_seen_vid, __ATOMIC_ACQUIRE),
        .last_seen_pid =
            __atomic_load_n(&s_last_seen_pid, __ATOMIC_ACQUIRE),
        .last_config_total_length =
            __atomic_load_n(&s_last_config_total_length, __ATOMIC_ACQUIRE),
        .last_probe_stage =
            __atomic_load_n(&s_last_probe_stage, __ATOMIC_ACQUIRE),
        .last_probe_address =
            __atomic_load_n(&s_last_probe_address, __ATOMIC_ACQUIRE),
        .last_parent_port =
            __atomic_load_n(&s_last_parent_port, __ATOMIC_ACQUIRE),
        .last_direct_root =
            __atomic_load_n(&s_last_direct_root, __ATOMIC_ACQUIRE),
        .registered =
            __atomic_load_n(&s_registered, __ATOMIC_ACQUIRE),
        .connected = controller_usb_host_is_connected(),
        .accepting_midi_out =
            __atomic_load_n(&s_accepting_out, __ATOMIC_ACQUIRE),
    };
}

esp_err_t controller_usb_host_write_audio(const int16_t *master_samples,
                                          const int16_t *headphone_samples,
                                          size_t frame_count,
                                          uint32_t source_sample_rate)
{
    return controller_usb_audio_stream_write(
        master_samples, headphone_samples, frame_count, source_sample_rate);
}

void controller_usb_host_get_audio_stats(
    controller_usb_host_audio_stats_t *stats_out)
{
    if (!stats_out) {
        return;
    }
    controller_usb_audio_stream_stats_t stream_stats = {0};
    controller_usb_audio_stream_get_stats(&stream_stats);
    *stats_out = (controller_usb_host_audio_stats_t) {
        .submitted_blocks = stream_stats.submitted_blocks,
        .dropped_blocks = stream_stats.dropped_blocks,
        .submitted_frames = stream_stats.submitted_frames,
        .ring_queued_frames = stream_stats.ring_queued_frames,
        .ring_capacity_frames = stream_stats.ring_capacity_frames,
        .ring_high_water_frames = stream_stats.ring_high_water_frames,
        .overrun_frames = stream_stats.overrun_frames,
        .underrun_frames = stream_stats.underrun_frames,
        .clock_trimmed_frames = stream_stats.clock_trimmed_frames,
        .clock_duplicated_frames = stream_stats.clock_duplicated_frames,
        .config_failures = stream_stats.config_failures,
        .transfer_failures = stream_stats.transfer_failures,
        .claimed = stream_stats.claimed,
        .configuring = stream_stats.configuring,
        .streaming = stream_stats.streaming,
        .faulted = stream_stats.faulted,
    };
}
