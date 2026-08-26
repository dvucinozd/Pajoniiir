#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "flx4_midi_host.h"
#include "control_link.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "usb/usb_host.h"

typedef struct __attribute__((packed)) {
    usb_config_desc_t config;
    usb_intf_desc_t interface;
    usb_ep_desc_t in_endpoint;
    usb_ep_desc_t out_endpoint;
} fake_config_tree_t;

static const usb_device_desc_t s_device_desc = {
    .idVendor = FLX4_USB_VID,
    .idProduct = FLX4_USB_PID,
    .bNumConfigurations = 1u,
};
static const fake_config_tree_t s_config_tree = {
    .config = {
        .bLength = sizeof(usb_config_desc_t),
        .bDescriptorType = 2u,
        .wTotalLength = sizeof(fake_config_tree_t),
        .bNumInterfaces = 1u,
        .bConfigurationValue = 1u,
    },
    .interface = {
        .bLength = sizeof(usb_intf_desc_t),
        .bDescriptorType = USB_B_DESCRIPTOR_TYPE_INTERFACE,
        .bInterfaceNumber = 3u,
        .bAlternateSetting = 1u,
        .bNumEndpoints = 2u,
        .bInterfaceClass = USB_CLASS_AUDIO,
        .bInterfaceSubClass = 3u,
    },
    .in_endpoint = {
        .bLength = sizeof(usb_ep_desc_t),
        .bDescriptorType = USB_B_DESCRIPTOR_TYPE_ENDPOINT,
        .bEndpointAddress = 0x81u,
        .bmAttributes = USB_BM_ATTRIBUTES_XFER_BULK,
        .wMaxPacketSize = 64u,
    },
    .out_endpoint = {
        .bLength = sizeof(usb_ep_desc_t),
        .bDescriptorType = USB_B_DESCRIPTOR_TYPE_ENDPOINT,
        .bEndpointAddress = 0x01u,
        .bmAttributes = USB_BM_ATTRIBUTES_XFER_BULK,
        .wMaxPacketSize = 64u,
    },
};

static esp_err_t s_open_result;
static esp_err_t s_device_desc_result;
static esp_err_t s_config_desc_result;
static esp_err_t s_claim_result;
static esp_err_t s_release_result;
static esp_err_t s_close_result;
static esp_err_t s_alloc_result;
static int s_alloc_fail_call;
static esp_err_t s_submit_result;
static unsigned s_alloc_calls;
static unsigned s_free_calls;
static unsigned s_release_calls;
static unsigned s_close_calls;
static unsigned s_semantic_calls;
static bool s_queue_has_packet;
static uint8_t s_queue_packet[4];

static void fake_reset(void)
{
    s_open_result = ESP_OK;
    s_device_desc_result = ESP_OK;
    s_config_desc_result = ESP_OK;
    s_claim_result = ESP_OK;
    s_release_result = ESP_OK;
    s_close_result = ESP_OK;
    s_alloc_result = ESP_OK;
    s_alloc_fail_call = 0;
    s_submit_result = ESP_OK;
    s_alloc_calls = s_free_calls = s_release_calls = s_close_calls = 0u;
    s_semantic_calls = 0u;
    s_queue_has_packet = false;
    flx4_midi_host_production_test_reset();
}

static flx4_midi_host_production_snapshot_t snapshot(void)
{
    flx4_midi_host_production_snapshot_t state;
    flx4_midi_host_production_test_snapshot(&state);
    return state;
}

QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t item_size)
{
    assert(length > 0u && item_size == 4u);
    return (QueueHandle_t)(uintptr_t)1u;
}

int xQueueSend(QueueHandle_t queue, const void *item, TickType_t ticks)
{
    (void)queue;
    (void)ticks;
    if (s_queue_has_packet) return pdFALSE;
    memcpy(s_queue_packet, item, sizeof(s_queue_packet));
    s_queue_has_packet = true;
    return pdTRUE;
}

int xQueueReceive(QueueHandle_t queue, void *item, TickType_t ticks)
{
    (void)queue;
    (void)ticks;
    if (!s_queue_has_packet) return pdFALSE;
    memcpy(item, s_queue_packet, sizeof(s_queue_packet));
    s_queue_has_packet = false;
    return pdTRUE;
}

UBaseType_t uxQueueSpacesAvailable(QueueHandle_t queue)
{
    (void)queue;
    return s_queue_has_packet ? 63u : 64u;
}

int xQueueReset(QueueHandle_t queue)
{
    (void)queue;
    s_queue_has_packet = false;
    return pdPASS;
}

void vQueueDelete(QueueHandle_t queue) { (void)queue; }
int xTaskCreate(void (*task)(void *), const char *name, unsigned stack,
                void *arg, unsigned priority, TaskHandle_t *handle)
{
    (void)task; (void)name; (void)stack; (void)arg; (void)priority;
    if (handle) *handle = (TaskHandle_t)(uintptr_t)2u;
    return pdPASS;
}
TaskHandle_t xTaskGetCurrentTaskHandle(void) { return (TaskHandle_t)(uintptr_t)3u; }
unsigned ulTaskNotifyTake(int clear_on_exit, TickType_t ticks)
{ (void)clear_on_exit; (void)ticks; return 1u; }
int xTaskNotifyGive(TaskHandle_t task) { (void)task; return pdPASS; }
TickType_t xTaskGetTickCount(void) { return 0u; }
void vTaskDelete(TaskHandle_t task) { (void)task; }

esp_err_t usb_host_device_open(usb_host_client_handle_t client, uint8_t address,
                               usb_device_handle_t *out_device)
{
    assert(client != NULL && address != 0u);
    if (s_open_result == ESP_OK) *out_device = (usb_device_handle_t)(uintptr_t)4u;
    return s_open_result;
}
esp_err_t usb_host_device_info(usb_device_handle_t device, usb_device_info_t *out_info)
{
    (void)device;
    *out_info = (usb_device_info_t){.bConfigurationValue = 1u, .dev_addr = 1u};
    return ESP_OK;
}
esp_err_t usb_host_get_device_descriptor(usb_device_handle_t device,
                                         const usb_device_desc_t **out_desc)
{
    (void)device;
    if (s_device_desc_result == ESP_OK) *out_desc = &s_device_desc;
    return s_device_desc_result;
}
esp_err_t usb_host_get_active_config_descriptor(usb_device_handle_t device,
                                                const usb_config_desc_t **out_desc)
{
    (void)device;
    if (s_config_desc_result == ESP_OK) *out_desc = &s_config_tree.config;
    return s_config_desc_result;
}
esp_err_t usb_host_interface_claim(usb_host_client_handle_t client,
                                   usb_device_handle_t device,
                                   uint8_t interface_num, uint8_t alternate_setting)
{
    assert(client && device && interface_num == 3u && alternate_setting == 1u);
    return s_claim_result;
}
esp_err_t usb_host_interface_release(usb_host_client_handle_t client,
                                     usb_device_handle_t device,
                                     uint8_t interface_num)
{
    assert(client && device && interface_num == 3u);
    s_release_calls++;
    return s_release_result;
}
esp_err_t usb_host_device_close(usb_host_client_handle_t client,
                                usb_device_handle_t device)
{
    assert(client && device);
    s_close_calls++;
    return s_close_result;
}
esp_err_t usb_host_transfer_alloc(int data_buffer_size, int flags,
                                  usb_transfer_t **out_transfer)
{
    (void)flags;
    s_alloc_calls++;
    if (s_alloc_result != ESP_OK ||
        (s_alloc_fail_call > 0 && (int)s_alloc_calls == s_alloc_fail_call)) {
        return ESP_ERR_NO_MEM;
    }
    usb_transfer_t *transfer = calloc(1u, sizeof(*transfer));
    assert(transfer);
    transfer->data_buffer = calloc(1u, (size_t)data_buffer_size);
    assert(transfer->data_buffer);
    transfer->data_buffer_size = data_buffer_size;
    *out_transfer = transfer;
    return ESP_OK;
}
esp_err_t usb_host_transfer_free(usb_transfer_t *transfer)
{
    assert(transfer);
    s_free_calls++;
    free(transfer->data_buffer);
    free(transfer);
    return ESP_OK;
}
esp_err_t usb_host_transfer_submit(usb_transfer_t *transfer)
{
    assert(transfer && transfer->callback);
    return s_submit_result;
}
esp_err_t usb_host_client_unblock(usb_host_client_handle_t client)
{ (void)client; return ESP_OK; }
esp_err_t usb_host_install(const usb_host_config_t *config)
{ (void)config; return ESP_OK; }
esp_err_t usb_host_lib_handle_events(uint32_t timeout_ticks, uint32_t *event_flags)
{ (void)timeout_ticks; *event_flags = 0u; return ESP_OK; }
void usb_host_device_free_all(void) { }
esp_err_t usb_host_client_register(const usb_host_client_config_t *config,
                                   usb_host_client_handle_t *out_client)
{ (void)config; *out_client = (usb_host_client_handle_t)(uintptr_t)1u; return ESP_OK; }
esp_err_t usb_host_client_handle_events(usb_host_client_handle_t client,
                                        uint32_t timeout_ticks)
{ (void)client; (void)timeout_ticks; return ESP_OK; }

esp_err_t control_link_send_semantic(uint8_t type, uint8_t id, int16_t value)
{
    assert(type == CTRL_TYPE_STATE && id == CTRL_ID_FLX4_CONNECTION);
    assert(value == CTRL_FLX4_CONNECTED || value == CTRL_FLX4_DISCONNECTED);
    s_semantic_calls++;
    return ESP_OK;
}
esp_err_t control_link_send_descriptor_report(const ctrl_descriptor_report_t *report)
{ assert(report != NULL); return ESP_OK; }

static void close_after_failed_open(void)
{
    flx4_midi_host_production_snapshot_t state = snapshot();
    assert(state.opened || !state.claimed);
    flx4_midi_host_production_test_device_gone();
    if (state.in_transfer_active || state.out_transfer_active) {
        flx4_midi_host_production_test_complete_transfers(true);
    }
    assert(flx4_midi_host_production_test_close_step());
    state = snapshot();
    assert(!state.opened && !state.claimed && !state.in_transfer_allocated &&
           !state.out_transfer_allocated && !state.closing);
}

static void test_open_failure_stages_are_recoverable(void)
{
    fake_reset();
    s_device_desc_result = ESP_FAIL;
    assert(flx4_midi_host_production_test_open(1u) == ESP_FAIL);
    close_after_failed_open();
    assert(s_close_calls == 1u);

    fake_reset();
    s_claim_result = ESP_FAIL;
    assert(flx4_midi_host_production_test_open(1u) == ESP_FAIL);
    close_after_failed_open();
    assert(s_close_calls == 1u);

    fake_reset();
    s_alloc_fail_call = 1;
    assert(flx4_midi_host_production_test_open(1u) == ESP_ERR_NO_MEM);
    close_after_failed_open();
    assert(s_release_calls == 1u && s_close_calls == 1u);

    fake_reset();
    s_alloc_fail_call = 2;
    assert(flx4_midi_host_production_test_open(1u) == ESP_ERR_NO_MEM);
    close_after_failed_open();
    assert(s_free_calls == 1u && s_release_calls == 1u && s_close_calls == 1u);

    fake_reset();
    s_submit_result = ESP_FAIL;
    assert(flx4_midi_host_production_test_open(1u) == ESP_FAIL);
    close_after_failed_open();
    assert(s_free_calls == 2u && s_release_calls == 1u && s_close_calls == 1u);
}

static void test_disconnect_waits_for_both_callbacks_then_retries_release_close(void)
{
    static const uint8_t led_packet[4] = {0x09u, 0x90u, 0x0bu, 0x7fu};
    fake_reset();
    assert(flx4_midi_host_production_test_open(1u) == ESP_OK);
    flx4_midi_host_production_snapshot_t state = snapshot();
    assert(state.opened && state.claimed && state.in_transfer_active);
    assert(flx4_midi_host_send_packet(led_packet) == ESP_OK);
    flx4_midi_host_production_test_pump();
    state = snapshot();
    assert(state.out_transfer_active);

    flx4_midi_host_production_test_device_gone();
    assert(!flx4_midi_host_production_test_close_step());
    assert(s_release_calls == 0u && s_close_calls == 0u);

    flx4_midi_host_production_test_complete_transfers(true);
    state = snapshot();
    assert(!state.in_transfer_allocated && !state.out_transfer_allocated);

    s_release_result = ESP_FAIL;
    assert(!flx4_midi_host_production_test_close_step());
    assert(snapshot().claimed && s_close_calls == 0u);
    s_release_result = ESP_OK;
    s_close_result = ESP_FAIL;
    assert(!flx4_midi_host_production_test_close_step());
    assert(!snapshot().claimed && snapshot().opened);
    s_close_result = ESP_OK;
    assert(flx4_midi_host_production_test_close_step());
    state = snapshot();
    assert(!state.opened && !state.claimed && !state.closing);
    assert(s_semantic_calls >= 2u); /* connected then disconnected */
}

int main(void)
{
    test_open_failure_stages_are_recoverable();
    test_disconnect_waits_for_both_callbacks_then_retries_release_close();
    puts("FLX4 production USB lifecycle tests passed");
    return 0;
}
