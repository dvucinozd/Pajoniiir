#include "usb_midi_probe.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "usb/msc_host.h"
#include "usb/usb_host.h"

static const char *TAG = "p4_dual_usb";

#define USB_HOST_TASK_STACK       4096
#define USB_HOST_TASK_PRIORITY    4
#define MSC_DRIVER_TASK_STACK     4096
#define MSC_DRIVER_TASK_PRIORITY  5
#define MSC_OWNER_TASK_STACK      6144
#define MSC_OWNER_TASK_PRIORITY   4
#define MIDI_TASK_STACK           (8 * 1024)
#define MIDI_TASK_PRIORITY        5
#define STATUS_PERIOD_MS          10000u
#define MSC_READ_PERIOD_MS        250u
#define MIDI_TRANSFER_BYTES       64
#define EVENT_QUEUE_DEPTH         8u
#define PHASE1_SOAK_SECONDS       (30u * 60u)

#define USB_CLASS_MASS_STORAGE    0x08u
#define USB_SUBCLASS_ANY          0xFFu
#define CLASS_SEEN_MSC            (1u << 0)
#define CLASS_SEEN_MIDI           (1u << 1)
#define P4_DUAL_PERIPHERAL_MAP    ((1u << 0) | (1u << 1))

extern uint32_t usb_dwc_compat_bna_recovered_count(void);

static esp_err_t s_host_install_result = ESP_ERR_INVALID_STATE;
static esp_err_t s_midi_register_result = ESP_ERR_INVALID_STATE;
static QueueHandle_t s_msc_event_queue;
static QueueHandle_t s_probe_address_queue;

static uint32_t s_class_seen_mask;
static uint32_t s_probe_open_failures;
static uint32_t s_probe_descriptor_failures;
static uint32_t s_msc_connects;
static uint32_t s_msc_disconnects;
static uint32_t s_msc_reads_ok;
static uint32_t s_msc_reads_failed;
static uint32_t s_msc_event_drops;
static uint32_t s_midi_connects;
static uint32_t s_midi_disconnects;
static uint32_t s_midi_packets;
static uint32_t s_midi_bytes;
static uint32_t s_midi_parse_rejects;
static uint32_t s_midi_submit_failures;
static uint32_t s_probe_event_drops;
static uint32_t s_direct_root_port_mask;
static uint8_t s_msc_parent_port = UINT8_MAX;
static uint8_t s_midi_parent_port = UINT8_MAX;
static bool s_msc_active;
static bool s_midi_active;

static inline uint32_t counter_get(const uint32_t *value)
{
    return __atomic_load_n(value, __ATOMIC_ACQUIRE);
}

static inline void counter_inc(uint32_t *value)
{
    (void)__atomic_add_fetch(value, 1u, __ATOMIC_RELAXED);
}

static inline void value_set_u32(uint32_t *value, uint32_t new_value)
{
    __atomic_store_n(value, new_value, __ATOMIC_RELEASE);
}

static inline void value_set_bool(bool *value, bool new_value)
{
    __atomic_store_n(value, new_value, __ATOMIC_RELEASE);
}

static inline bool value_get_bool(const bool *value)
{
    return __atomic_load_n(value, __ATOMIC_ACQUIRE);
}

static const char *speed_name(usb_speed_t speed)
{
    switch (speed) {
    case USB_SPEED_LOW:
        return "low";
    case USB_SPEED_FULL:
        return "full";
    case USB_SPEED_HIGH:
        return "high";
    default:
        return "unknown";
    }
}

static void usb_string_to_ascii(const usb_str_desc_t *desc,
                                char *out,
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
    for (size_t i = 0; i < chars; ++i) {
        const uint16_t code_unit = desc->wData[i];
        out[i] = (code_unit >= 0x20u && code_unit <= 0x7Eu)
                     ? (char)code_unit
                     : '?';
    }
    out[chars] = '\0';
}

static void record_topology(const usb_device_info_t *info,
                            bool is_msc,
                            bool is_midi)
{
    if (!info) {
        return;
    }
    if (info->parent.dev_hdl == NULL && info->parent.port_num < 32u) {
        (void)__atomic_fetch_or(&s_direct_root_port_mask,
                                1u << info->parent.port_num,
                                __ATOMIC_RELAXED);
    }
    if (is_msc) {
        __atomic_store_n(&s_msc_parent_port,
                         info->parent.port_num,
                         __ATOMIC_RELEASE);
    }
    if (is_midi) {
        __atomic_store_n(&s_midi_parent_port,
                         info->parent.port_num,
                         __ATOMIC_RELEASE);
    }
}

static void usb_host_daemon_task(void *arg)
{
    TaskHandle_t starter = (TaskHandle_t)arg;
    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .root_port_unpowered = true,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
        .enum_filter_cb = NULL,
        .peripheral_map = P4_DUAL_PERIPHERAL_MAP,
    };

    s_host_install_result = usb_host_install(&host_config);
    xTaskNotifyGive(starter);
    if (s_host_install_result != ESP_OK) {
        ESP_LOGE(TAG, "usb_host_install(peripheral_map=0x%02X): %s",
                 P4_DUAL_PERIPHERAL_MAP,
                 esp_err_to_name(s_host_install_result));
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGW(TAG,
             "one USB Host Library installed for P4 USB0+USB1 (peripheral_map=0x%02X)",
             P4_DUAL_PERIPHERAL_MAP);

    for (;;) {
        uint32_t flags = 0u;
        const esp_err_t rc = usb_host_lib_handle_events(portMAX_DELAY, &flags);
        if (rc != ESP_OK && rc != ESP_ERR_TIMEOUT) {
            ESP_LOGE(TAG, "usb_host_lib_handle_events: %s", esp_err_to_name(rc));
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        if ((flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) != 0u) {
            ESP_LOGW(TAG, "Host Library has no clients");
        }
        if ((flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) != 0u) {
            ESP_LOGI(TAG, "Host Library released all removed devices");
        }
    }
}

typedef struct {
    msc_host_event_t event;
} msc_queue_event_t;

static void msc_event_callback(const msc_host_event_t *event, void *arg)
{
    (void)arg;
    if (!event || !s_msc_event_queue) {
        return;
    }
    const msc_queue_event_t queued = { .event = *event };
    if (xQueueSend(s_msc_event_queue, &queued, 0) != pdTRUE) {
        counter_inc(&s_msc_event_drops);
    }
}

static uint32_t fnv1a32(const uint8_t *data, size_t len)
{
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < len; ++i) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

static void msc_release_device(msc_host_device_handle_t *device,
                               uint8_t **sector_buffer)
{
    if (*sector_buffer) {
        heap_caps_free(*sector_buffer);
        *sector_buffer = NULL;
    }
    if (*device) {
        const esp_err_t rc = msc_host_uninstall_device(*device);
        if (rc != ESP_OK) {
            ESP_LOGW(TAG, "msc_host_uninstall_device: %s", esp_err_to_name(rc));
        }
        *device = NULL;
    }
    value_set_bool(&s_msc_active, false);
}

static esp_err_t msc_read_one_sector(msc_host_device_handle_t device,
                                     const msc_host_device_info_t *info,
                                     uint8_t *buffer,
                                     uint32_t sector)
{
    if (!device || !info || !buffer || info->sector_size == 0u ||
        sector >= info->sector_count) {
        return ESP_ERR_INVALID_ARG;
    }
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    const esp_err_t rc =
        msc_host_read_sector(device, sector, buffer, info->sector_size);
#pragma GCC diagnostic pop
    return rc;
}

static void msc_owner_task(void *arg)
{
    (void)arg;
    msc_host_device_handle_t device = NULL;
    msc_host_device_info_t info = {0};
    uint8_t *sector_buffer = NULL;
    uint32_t next_sector = 0u;

    for (;;) {
        msc_queue_event_t queued;
        const BaseType_t got_event =
            xQueueReceive(s_msc_event_queue,
                          &queued,
                          pdMS_TO_TICKS(MSC_READ_PERIOD_MS));

        if (got_event == pdTRUE) {
            if (queued.event.event == MSC_DEVICE_CONNECTED) {
                const uint8_t address = queued.event.device.address;
                if (device) {
                    ESP_LOGW(TAG,
                             "additional MSC device ignored in Phase 1 (addr=%u)",
                             (unsigned)address);
                    continue;
                }

                ESP_LOGI(TAG, "MSC class driver connected addr=%u", (unsigned)address);
                esp_err_t rc = msc_host_install_device(address, &device);
                if (rc != ESP_OK) {
                    ESP_LOGE(TAG, "msc_host_install_device: %s", esp_err_to_name(rc));
                    device = NULL;
                    counter_inc(&s_msc_reads_failed);
                    continue;
                }
                rc = msc_host_get_device_info(device, &info);
                if (rc != ESP_OK || info.sector_size == 0u || info.sector_count == 0u) {
                    ESP_LOGE(TAG, "msc_host_get_device_info: %s", esp_err_to_name(rc));
                    msc_release_device(&device, &sector_buffer);
                    counter_inc(&s_msc_reads_failed);
                    continue;
                }

                sector_buffer = heap_caps_malloc(info.sector_size,
                                                  MALLOC_CAP_DMA |
                                                  MALLOC_CAP_INTERNAL);
                if (!sector_buffer) {
                    ESP_LOGE(TAG,
                             "cannot allocate %u-byte DMA sector buffer",
                             (unsigned)info.sector_size);
                    msc_release_device(&device, &sector_buffer);
                    counter_inc(&s_msc_reads_failed);
                    continue;
                }

                counter_inc(&s_msc_connects);
                value_set_bool(&s_msc_active, true);
                next_sector = 0u;
                const uint64_t size_mib =
                    ((uint64_t)info.sector_count * info.sector_size) /
                    (1024u * 1024u);
                ESP_LOGW(TAG,
                         "MSC READY addr=%u VID=0x%04X PID=0x%04X size=%llu MiB sector=%u",
                         (unsigned)address,
                         info.idVendor,
                         info.idProduct,
                         (unsigned long long)size_mib,
                         (unsigned)info.sector_size);
            } else if (queued.event.event == MSC_DEVICE_DISCONNECTED) {
                if (device && queued.event.device.handle == device) {
                    ESP_LOGW(TAG, "MSC DISCONNECTED");
                    counter_inc(&s_msc_disconnects);
                    msc_release_device(&device, &sector_buffer);
                    memset(&info, 0, sizeof(info));
                    next_sector = 0u;
                }
            }
        }

        if (!device || !sector_buffer || !value_get_bool(&s_msc_active)) {
            continue;
        }

        const uint32_t probe_span = info.sector_count < 2048u
                                        ? info.sector_count
                                        : 2048u;
        if (probe_span == 0u) {
            continue;
        }
        const uint32_t sector = next_sector % probe_span;
        const esp_err_t rc =
            msc_read_one_sector(device, &info, sector_buffer, sector);
        if (rc == ESP_OK) {
            counter_inc(&s_msc_reads_ok);
            const uint32_t count = counter_get(&s_msc_reads_ok);
            if (count <= 4u || (count % 128u) == 0u) {
                const size_t hash_len = info.sector_size < 64u
                                            ? info.sector_size
                                            : 64u;
                ESP_LOGI(TAG,
                         "MSC READ OK count=%" PRIu32 " sector=%" PRIu32
                         " hash64=0x%08" PRIX32,
                         count,
                         sector,
                         fnv1a32(sector_buffer, hash_len));
            }
            next_sector++;
        } else {
            counter_inc(&s_msc_reads_failed);
            ESP_LOGW(TAG,
                     "MSC read failed sector=%" PRIu32 ": %s",
                     sector,
                     esp_err_to_name(rc));
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
}

typedef struct {
    usb_host_client_handle_t client;
    usb_device_handle_t device;
    usb_transfer_t *in_transfer;
    usb_midi_endpoints_t endpoints;
    uint8_t address;
    bool opened;
    bool claimed;
    bool transfer_active;
    bool closing;
} midi_host_state_t;

static midi_host_state_t s_midi;

static void midi_transfer_callback(usb_transfer_t *transfer)
{
    midi_host_state_t *state = (midi_host_state_t *)transfer->context;
    state->transfer_active = false;

    if (transfer->status == USB_TRANSFER_STATUS_COMPLETED) {
        counter_inc(&s_midi_bytes); /* incremented once below to preserve atomic helper use */
        (void)__atomic_sub_fetch(&s_midi_bytes, 1u, __ATOMIC_RELAXED);
        (void)__atomic_add_fetch(&s_midi_bytes,
                                 (uint32_t)transfer->actual_num_bytes,
                                 __ATOMIC_RELAXED);
        for (int offset = 0; offset + 3 < transfer->actual_num_bytes; offset += 4) {
            usb_midi_message_t message;
            const uint8_t *packet = &transfer->data_buffer[offset];
            if (!usb_midi_parse_event_packet(packet, &message)) {
                counter_inc(&s_midi_parse_rejects);
                continue;
            }
            counter_inc(&s_midi_packets);
            const uint32_t count = counter_get(&s_midi_packets);
            if (count <= 24u || (count % 256u) == 0u) {
                ESP_LOGI(TAG,
                         "MIDI count=%" PRIu32
                         " cable=%u cin=0x%X len=%u %02X %02X %02X",
                         count,
                         message.cable,
                         message.cin,
                         message.len,
                         message.status,
                         message.data1,
                         message.data2);
            }
        }
    } else if (transfer->status != USB_TRANSFER_STATUS_NO_DEVICE &&
               transfer->status != USB_TRANSFER_STATUS_CANCELED) {
        ESP_LOGW(TAG,
                 "MIDI IN transfer status=%d bytes=%d",
                 (int)transfer->status,
                 transfer->actual_num_bytes);
    }

    if (state->closing || transfer->status == USB_TRANSFER_STATUS_NO_DEVICE) {
        return;
    }

    transfer->num_bytes =
        usb_round_up_to_mps(MIDI_TRANSFER_BYTES, state->endpoints.in_ep_mps);
    const esp_err_t rc = usb_host_transfer_submit(transfer);
    if (rc == ESP_OK) {
        state->transfer_active = true;
    } else {
        counter_inc(&s_midi_submit_failures);
        ESP_LOGE(TAG, "MIDI IN resubmit: %s", esp_err_to_name(rc));
    }
}

static esp_err_t midi_submit_if_idle(midi_host_state_t *state)
{
    if (!state->opened || !state->claimed || state->closing ||
        !state->in_transfer || state->transfer_active) {
        return ESP_OK;
    }
    state->in_transfer->device_handle = state->device;
    state->in_transfer->bEndpointAddress = state->endpoints.in_ep_addr;
    state->in_transfer->num_bytes =
        usb_round_up_to_mps(MIDI_TRANSFER_BYTES, state->endpoints.in_ep_mps);
    const esp_err_t rc = usb_host_transfer_submit(state->in_transfer);
    if (rc == ESP_OK) {
        state->transfer_active = true;
    } else {
        counter_inc(&s_midi_submit_failures);
    }
    return rc;
}

static void midi_close_step(midi_host_state_t *state)
{
    state->closing = true;
    if (state->transfer_active) {
        return;
    }
    if (state->in_transfer) {
        const esp_err_t rc = usb_host_transfer_free(state->in_transfer);
        if (rc != ESP_OK) {
            ESP_LOGW(TAG, "usb_host_transfer_free: %s", esp_err_to_name(rc));
            return;
        }
        state->in_transfer = NULL;
    }
    if (state->claimed) {
        const esp_err_t rc = usb_host_interface_release(state->client,
                                                        state->device,
                                                        state->endpoints.interface_num);
        if (rc != ESP_OK) {
            ESP_LOGW(TAG, "usb_host_interface_release: %s", esp_err_to_name(rc));
            return;
        }
        state->claimed = false;
    }
    if (state->opened) {
        const esp_err_t rc = usb_host_device_close(state->client, state->device);
        if (rc != ESP_OK) {
            ESP_LOGW(TAG, "usb_host_device_close: %s", esp_err_to_name(rc));
            return;
        }
        state->opened = false;
        state->device = NULL;
    }

    memset(&state->endpoints, 0, sizeof(state->endpoints));
    state->address = 0u;
    state->closing = false;
    value_set_bool(&s_midi_active, false);
    counter_inc(&s_midi_disconnects);
    ESP_LOGW(TAG, "MIDI DEVICE CLOSED");
}

static void midi_client_event_callback(const usb_host_client_event_msg_t *event,
                                       void *arg)
{
    midi_host_state_t *state = (midi_host_state_t *)arg;
    switch (event->event) {
    case USB_HOST_CLIENT_EVENT_NEW_DEV:
        if (xQueueSend(s_probe_address_queue,
                       &event->new_dev.address,
                       0) != pdTRUE) {
            counter_inc(&s_probe_event_drops);
        }
        break;
    case USB_HOST_CLIENT_EVENT_DEV_GONE:
        if (state->opened && event->dev_gone.dev_hdl == state->device) {
            state->closing = true;
        }
        break;
    case USB_HOST_CLIENT_EVENT_DEV_REMOVED:
        ESP_LOGI(TAG,
                 "Host Library removed address=%u",
                 (unsigned)event->dev_removed.address);
        break;
    case USB_HOST_CLIENT_EVENT_DEV_SUSPENDED:
    case USB_HOST_CLIENT_EVENT_DEV_RESUMED:
    default:
        break;
    }
}

static esp_err_t probe_and_optionally_claim_midi(midi_host_state_t *state,
                                                  uint8_t address)
{
    usb_device_handle_t device = NULL;
    esp_err_t rc = usb_host_device_open(state->client, address, &device);
    if (rc != ESP_OK) {
        counter_inc(&s_probe_open_failures);
        ESP_LOGW(TAG,
                 "probe open addr=%u: %s",
                 (unsigned)address,
                 esp_err_to_name(rc));
        return rc;
    }

    usb_device_info_t info = {0};
    const usb_device_desc_t *device_desc = NULL;
    const usb_config_desc_t *config_desc = NULL;
    rc = usb_host_device_info(device, &info);
    if (rc == ESP_OK) {
        rc = usb_host_get_device_descriptor(device, &device_desc);
    }
    if (rc == ESP_OK) {
        rc = usb_host_get_active_config_descriptor(device, &config_desc);
    }
    if (rc != ESP_OK || !device_desc || !config_desc) {
        counter_inc(&s_probe_descriptor_failures);
        ESP_LOGW(TAG,
                 "probe descriptors addr=%u: %s",
                 (unsigned)address,
                 esp_err_to_name(rc));
        (void)usb_host_device_close(state->client, device);
        return rc == ESP_OK ? ESP_FAIL : rc;
    }

    const size_t config_len = config_desc->wTotalLength;
    usb_midi_endpoints_t endpoints;
    const bool has_midi = usb_midi_find_streaming_endpoints(
        (const uint8_t *)config_desc, config_len, &endpoints);
    const bool has_msc = usb_midi_config_has_interface_class(
        (const uint8_t *)config_desc,
        config_len,
        USB_CLASS_MASS_STORAGE,
        USB_SUBCLASS_ANY);

    char product[64];
    usb_string_to_ascii(info.str_desc_product, product, sizeof(product));
    ESP_LOGW(TAG,
             "PROBE addr=%u speed=%s VID=0x%04X PID=0x%04X product='%s' "
             "parent=%p parent_port=%u direct_root=%u MSC=%u MIDI=%u",
             (unsigned)address,
             speed_name(info.speed),
             device_desc->idVendor,
             device_desc->idProduct,
             product,
             (void *)info.parent.dev_hdl,
             (unsigned)info.parent.port_num,
             info.parent.dev_hdl == NULL ? 1u : 0u,
             has_msc ? 1u : 0u,
             has_midi ? 1u : 0u);

    if (has_msc) {
        (void)__atomic_fetch_or(&s_class_seen_mask,
                                CLASS_SEEN_MSC,
                                __ATOMIC_RELAXED);
    }
    if (has_midi) {
        (void)__atomic_fetch_or(&s_class_seen_mask,
                                CLASS_SEEN_MIDI,
                                __ATOMIC_RELAXED);
    }
    record_topology(&info, has_msc, has_midi);

    if (!has_midi) {
        (void)usb_host_device_close(state->client, device);
        return ESP_ERR_NOT_FOUND;
    }
    if (state->opened || state->claimed || state->device) {
        ESP_LOGW(TAG,
                 "additional MIDI device ignored addr=%u",
                 (unsigned)address);
        (void)usb_host_device_close(state->client, device);
        return ESP_ERR_INVALID_STATE;
    }

    state->device = device;
    state->address = address;
    state->opened = true;
    state->closing = false;
    state->endpoints = endpoints;

    rc = usb_host_interface_claim(state->client,
                                  state->device,
                                  endpoints.interface_num,
                                  endpoints.alternate_setting);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "claim MIDI interface: %s", esp_err_to_name(rc));
        state->closing = true;
        midi_close_step(state);
        return rc;
    }
    state->claimed = true;

    const int transfer_size =
        usb_round_up_to_mps(MIDI_TRANSFER_BYTES, endpoints.in_ep_mps);
    rc = usb_host_transfer_alloc(transfer_size, 0, &state->in_transfer);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "allocate MIDI IN transfer: %s", esp_err_to_name(rc));
        state->closing = true;
        midi_close_step(state);
        return rc;
    }
    state->in_transfer->device_handle = state->device;
    state->in_transfer->bEndpointAddress = endpoints.in_ep_addr;
    state->in_transfer->callback = midi_transfer_callback;
    state->in_transfer->context = state;
    state->in_transfer->num_bytes = transfer_size;

    rc = midi_submit_if_idle(state);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "submit MIDI IN transfer: %s", esp_err_to_name(rc));
        state->closing = true;
        midi_close_step(state);
        return rc;
    }

    counter_inc(&s_midi_connects);
    value_set_bool(&s_midi_active, true);
    ESP_LOGW(TAG,
             "MIDI READY addr=%u interface=%u alt=%u IN=0x%02X/%u OUT=0x%02X/%u",
             (unsigned)address,
             endpoints.interface_num,
             endpoints.alternate_setting,
             endpoints.in_ep_addr,
             endpoints.in_ep_mps,
             endpoints.out_ep_addr,
             endpoints.out_ep_mps);
    return ESP_OK;
}

static void midi_client_task(void *arg)
{
    TaskHandle_t starter = (TaskHandle_t)arg;
    memset(&s_midi, 0, sizeof(s_midi));

    const usb_host_client_config_t client_config = {
        .is_synchronous = false,
        .max_num_event_msg = EVENT_QUEUE_DEPTH,
        .flags = {
            .notify_dev_removed = 1u,
        },
        .async = {
            .client_event_callback = midi_client_event_callback,
            .callback_arg = &s_midi,
        },
    };

    s_midi_register_result =
        usb_host_client_register(&client_config, &s_midi.client);
    xTaskNotifyGive(starter);
    if (s_midi_register_result != ESP_OK) {
        ESP_LOGE(TAG,
                 "usb_host_client_register: %s",
                 esp_err_to_name(s_midi_register_result));
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "raw USB probe/MIDI client registered");
    for (;;) {
        const esp_err_t rc =
            usb_host_client_handle_events(s_midi.client, pdMS_TO_TICKS(100));
        if (rc != ESP_OK && rc != ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "usb_host_client_handle_events: %s", esp_err_to_name(rc));
        }

        if (s_midi.closing) {
            midi_close_step(&s_midi);
            continue;
        }

        uint8_t address = 0u;
        while (!s_midi.closing &&
               xQueueReceive(s_probe_address_queue, &address, 0) == pdTRUE) {
            const esp_err_t probe_rc =
                probe_and_optionally_claim_midi(&s_midi, address);
            if (probe_rc != ESP_OK && probe_rc != ESP_ERR_NOT_FOUND &&
                probe_rc != ESP_ERR_INVALID_STATE) {
                ESP_LOGW(TAG,
                         "device probe addr=%u ended with %s",
                         (unsigned)address,
                         esp_err_to_name(probe_rc));
            }
        }

        if (!s_midi.closing && s_midi.opened && s_midi.claimed &&
            !s_midi.transfer_active) {
            const esp_err_t pump_rc = midi_submit_if_idle(&s_midi);
            if (pump_rc != ESP_OK) {
                ESP_LOGW(TAG, "MIDI pump: %s", esp_err_to_name(pump_rc));
                vTaskDelay(pdMS_TO_TICKS(20));
            }
        }
    }
}

static void phase1_status_loop(void)
{
    const int64_t start_us = esp_timer_get_time();
    int64_t dual_since_us = 0;
    bool soak_reported = false;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(STATUS_PERIOD_MS));

        usb_host_lib_info_t host_info = {0};
        const esp_err_t info_rc = usb_host_lib_info(&host_info);
        const bool msc_active = value_get_bool(&s_msc_active);
        const bool midi_active = value_get_bool(&s_midi_active);
        const int64_t now_us = esp_timer_get_time();
        if (msc_active && midi_active) {
            if (dual_since_us == 0) {
                dual_since_us = now_us;
                ESP_LOGW(TAG, "PHASE1 DUAL-ACTIVE WINDOW STARTED");
            }
        } else {
            dual_since_us = 0;
            soak_reported = false;
        }
        const uint32_t dual_seconds = dual_since_us == 0
                                          ? 0u
                                          : (uint32_t)((now_us - dual_since_us) /
                                                       1000000ll);

        const uint8_t msc_parent =
            __atomic_load_n(&s_msc_parent_port, __ATOMIC_ACQUIRE);
        const uint8_t midi_parent =
            __atomic_load_n(&s_midi_parent_port, __ATOMIC_ACQUIRE);
        ESP_LOGW(TAG,
                 "PHASE1 STATUS uptime=%" PRIu32 "s dual=%" PRIu32
                 "s host_rc=%s devices=%d clients=%d class_mask=0x%02" PRIX32
                 " MSC(active=%u conn=%" PRIu32 " disc=%" PRIu32
                 " read_ok=%" PRIu32 " read_fail=%" PRIu32 ")"
                 " MIDI(active=%u conn=%" PRIu32 " disc=%" PRIu32
                 " packets=%" PRIu32 " bytes=%" PRIu32
                 " reject=%" PRIu32 " submit_fail=%" PRIu32 ")"
                 " topology(msc_parent=%u midi_parent=%u root_mask=0x%08" PRIX32 ")"
                 " drops(msc=%" PRIu32 " probe=%" PRIu32 ") bna_recovered=%" PRIu32,
                 (uint32_t)((now_us - start_us) / 1000000ll),
                 dual_seconds,
                 info_rc == ESP_OK ? "OK" : esp_err_to_name(info_rc),
                 info_rc == ESP_OK ? host_info.num_devices : -1,
                 info_rc == ESP_OK ? host_info.num_clients : -1,
                 counter_get(&s_class_seen_mask),
                 msc_active ? 1u : 0u,
                 counter_get(&s_msc_connects),
                 counter_get(&s_msc_disconnects),
                 counter_get(&s_msc_reads_ok),
                 counter_get(&s_msc_reads_failed),
                 midi_active ? 1u : 0u,
                 counter_get(&s_midi_connects),
                 counter_get(&s_midi_disconnects),
                 counter_get(&s_midi_packets),
                 counter_get(&s_midi_bytes),
                 counter_get(&s_midi_parse_rejects),
                 counter_get(&s_midi_submit_failures),
                 msc_parent,
                 midi_parent,
                 counter_get(&s_direct_root_port_mask),
                 counter_get(&s_msc_event_drops),
                 counter_get(&s_probe_event_drops),
                 usb_dwc_compat_bna_recovered_count());

        if (!soak_reported && dual_seconds >= PHASE1_SOAK_SECONDS &&
            counter_get(&s_msc_reads_ok) > 0u &&
            counter_get(&s_midi_packets) > 0u &&
            counter_get(&s_msc_event_drops) == 0u &&
            counter_get(&s_probe_event_drops) == 0u) {
            soak_reported = true;
            ESP_LOGW(TAG,
                     "PHASE1 30-MINUTE DUAL-HOST SOAK REACHED: inspect disconnect/reconnect evidence before acceptance");
        }
    }
}

void app_main(void)
{
    ESP_LOGW(TAG, "Pajoniiir Phase 1 ESP32-P4 dual USB host spike");
    ESP_LOGW(TAG,
             "Expected wiring: P4 USB0 HS=Rekordbox MSC, P4 USB1 FS=DJ controller MIDI");
    ESP_LOGW(TAG,
             "This isolated image does not mount FATFS, start playback, translate controls or send LEDs");

    s_msc_event_queue =
        xQueueCreate(EVENT_QUEUE_DEPTH, sizeof(msc_queue_event_t));
    s_probe_address_queue =
        xQueueCreate(EVENT_QUEUE_DEPTH, sizeof(uint8_t));
    ESP_ERROR_CHECK(s_msc_event_queue && s_probe_address_queue
                        ? ESP_OK
                        : ESP_ERR_NO_MEM);

    const TaskHandle_t starter = xTaskGetCurrentTaskHandle();
    BaseType_t created = xTaskCreate(usb_host_daemon_task,
                                     "usb_hostd",
                                     USB_HOST_TASK_STACK,
                                     starter,
                                     USB_HOST_TASK_PRIORITY,
                                     NULL);
    ESP_ERROR_CHECK(created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5000)) != 0u
                        ? ESP_OK
                        : ESP_ERR_TIMEOUT);
    ESP_ERROR_CHECK(s_host_install_result);

    const msc_host_driver_config_t msc_config = {
        .create_backround_task = true,
        .task_priority = MSC_DRIVER_TASK_PRIORITY,
        .stack_size = MSC_DRIVER_TASK_STACK,
        .core_id = tskNO_AFFINITY,
        .callback = msc_event_callback,
        .callback_arg = NULL,
    };
    ESP_ERROR_CHECK(msc_host_install(&msc_config));

    created = xTaskCreate(msc_owner_task,
                          "msc_probe",
                          MSC_OWNER_TASK_STACK,
                          NULL,
                          MSC_OWNER_TASK_PRIORITY,
                          NULL);
    ESP_ERROR_CHECK(created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);

    created = xTaskCreate(midi_client_task,
                          "usb_midi",
                          MIDI_TASK_STACK,
                          starter,
                          MIDI_TASK_PRIORITY,
                          NULL);
    ESP_ERROR_CHECK(created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5000)) != 0u
                        ? ESP_OK
                        : ESP_ERR_TIMEOUT);
    ESP_ERROR_CHECK(s_midi_register_result);

    const esp_err_t power_rc = usb_host_lib_set_root_port_power(true);
    if (power_rc != ESP_OK && power_rc != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(power_rc);
    }
    ESP_LOGW(TAG,
             "both root ports powered through the current global API; connect both fixtures");

    phase1_status_loop();
}
