#pragma once

#include <stdint.h>

#include "esp_err.h"

typedef void *usb_host_client_handle_t;
typedef void *usb_device_handle_t;

static inline esp_err_t usb_host_interface_claim(usb_host_client_handle_t client,
                                                 usb_device_handle_t device,
                                                 uint8_t bInterfaceNumber,
                                                 uint8_t bAlternateSetting)
{
    (void)client;
    (void)device;
    (void)bInterfaceNumber;
    (void)bAlternateSetting;
    return ESP_OK;
}
