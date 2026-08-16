#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef void *usb_host_client_handle_t;
typedef void *usb_device_handle_t;

typedef enum {
    USB_TRANSFER_STATUS_COMPLETED = 0,
    USB_TRANSFER_STATUS_ERROR,
    USB_TRANSFER_STATUS_NO_DEVICE,
} usb_transfer_status_t;

typedef struct usb_transfer_t usb_transfer_t;
struct usb_transfer_t {
    usb_device_handle_t device_handle;
    uint8_t bEndpointAddress;
    void (*callback)(usb_transfer_t *transfer);
    void *context;
    int num_bytes;
    int actual_num_bytes;
    int data_buffer_size;
    uint8_t *data_buffer;
    usb_transfer_status_t status;
};

typedef struct __attribute__((packed)) {
    uint8_t bLength;
    uint8_t bDescriptorType;
} usb_standard_desc_t;

typedef struct __attribute__((packed)) {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t wTotalLength;
    uint8_t bNumInterfaces;
    uint8_t bConfigurationValue;
    uint8_t iConfiguration;
    uint8_t bmAttributes;
    uint8_t bMaxPower;
} usb_config_desc_t;

typedef struct __attribute__((packed)) {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bInterfaceNumber;
    uint8_t bAlternateSetting;
    uint8_t bNumEndpoints;
    uint8_t bInterfaceClass;
    uint8_t bInterfaceSubClass;
    uint8_t bInterfaceProtocol;
    uint8_t iInterface;
} usb_intf_desc_t;

typedef struct __attribute__((packed)) {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bEndpointAddress;
    uint8_t bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t bInterval;
} usb_ep_desc_t;

typedef struct {
    uint16_t idVendor;
    uint16_t idProduct;
    uint8_t bDeviceClass;
    uint8_t bDeviceSubClass;
    uint8_t bNumConfigurations;
} usb_device_desc_t;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t wData[31];
} usb_str_desc_t;

typedef struct {
    int speed;
    uint8_t bConfigurationValue;
    uint8_t dev_addr;
    const usb_str_desc_t *str_desc_product;
} usb_device_info_t;

#define USB_STANDARD_DESC_SIZE 2
#define USB_INTF_DESC_SIZE 9
#define USB_EP_DESC_SIZE 7
#define USB_B_DESCRIPTOR_TYPE_INTERFACE 4
#define USB_B_DESCRIPTOR_TYPE_ENDPOINT 5
#define USB_CLASS_AUDIO 1
#define USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK 0x80
#define USB_BM_ATTRIBUTES_XFERTYPE_MASK 0x03
#define USB_BM_ATTRIBUTES_XFER_BULK 0x02
#define USB_BM_ATTRIBUTES_XFER_INT 0x03
#define USB_EP_DESC_GET_MPS(ep) ((ep)->wMaxPacketSize)

typedef enum {
    USB_HOST_CLIENT_EVENT_NEW_DEV = 0,
    USB_HOST_CLIENT_EVENT_DEV_GONE,
} usb_host_client_event_t;

typedef struct {
    usb_host_client_event_t event;
    struct { uint8_t address; } new_dev;
} usb_host_client_event_msg_t;

typedef struct {
    bool skip_phy_setup;
    int intr_flags;
} usb_host_config_t;

typedef struct {
    bool is_synchronous;
    unsigned max_num_event_msg;
    struct {
        void (*client_event_callback)(const usb_host_client_event_msg_t *, void *);
        void *callback_arg;
    } async;
} usb_host_client_config_t;

#define USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS 1u

esp_err_t usb_host_device_open(usb_host_client_handle_t client, uint8_t address,
                               usb_device_handle_t *out_device);
esp_err_t usb_host_device_info(usb_device_handle_t device, usb_device_info_t *out_info);
esp_err_t usb_host_get_device_descriptor(usb_device_handle_t device,
                                         const usb_device_desc_t **out_desc);
esp_err_t usb_host_get_active_config_descriptor(usb_device_handle_t device,
                                                const usb_config_desc_t **out_desc);
esp_err_t usb_host_interface_claim(usb_host_client_handle_t client,
                                   usb_device_handle_t device,
                                   uint8_t interface_num, uint8_t alternate_setting);
esp_err_t usb_host_interface_release(usb_host_client_handle_t client,
                                     usb_device_handle_t device,
                                     uint8_t interface_num);
esp_err_t usb_host_device_close(usb_host_client_handle_t client,
                                usb_device_handle_t device);
esp_err_t usb_host_transfer_alloc(int data_buffer_size, int flags,
                                  usb_transfer_t **out_transfer);
esp_err_t usb_host_transfer_free(usb_transfer_t *transfer);
esp_err_t usb_host_transfer_submit(usb_transfer_t *transfer);
esp_err_t usb_host_client_unblock(usb_host_client_handle_t client);
esp_err_t usb_host_install(const usb_host_config_t *config);
esp_err_t usb_host_lib_handle_events(uint32_t timeout_ticks, uint32_t *event_flags);
void usb_host_device_free_all(void);
esp_err_t usb_host_client_register(const usb_host_client_config_t *config,
                                   usb_host_client_handle_t *out_client);
esp_err_t usb_host_client_handle_events(usb_host_client_handle_t client,
                                        uint32_t timeout_ticks);
