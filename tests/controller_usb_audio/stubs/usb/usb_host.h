
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include "esp_err.h"
typedef void *usb_host_client_handle_t;
typedef void *usb_device_handle_t;
typedef struct {
    uint8_t bmRequestType, bRequest;
    uint16_t wValue, wIndex, wLength;
} usb_setup_packet_t;
typedef struct { int num_bytes, actual_num_bytes, status; } usb_isoc_packet_desc_t;
typedef struct usb_transfer {
    uint8_t *data_buffer;
    size_t data_buffer_size;
    void *device_handle;
    uint8_t bEndpointAddress;
    int num_bytes, actual_num_bytes, status, num_isoc_packets;
    void (*callback)(struct usb_transfer *);
    usb_isoc_packet_desc_t isoc_packet_desc[4];
} usb_transfer_t;
#define USB_TRANSFER_STATUS_COMPLETED 0
#define USB_TRANSFER_STATUS_CANCELED 1
#define USB_TRANSFER_STATUS_NO_DEVICE 2
#define USB_TRANSFER_STATUS_ERROR 3
#define USB_TRANSFER_STATUS_SKIPPED 4
#define USB_BM_REQUEST_TYPE_DIR_OUT 0
#define USB_BM_REQUEST_TYPE_TYPE_STANDARD 0
#define USB_BM_REQUEST_TYPE_RECIP_INTERFACE 1
#define USB_BM_REQUEST_TYPE_TYPE_CLASS 0x20
#define USB_BM_REQUEST_TYPE_RECIP_ENDPOINT 2
#define USB_B_REQUEST_SET_INTERFACE 11
extern unsigned review_submits;
static inline int usb_host_transfer_submit(usb_transfer_t *t) {
    (void)t; ++review_submits; return ESP_OK;
}
static inline int usb_host_transfer_submit_control(void *c, usb_transfer_t *t) {
    (void)c; (void)t; return ESP_OK;
}
static inline int usb_host_transfer_alloc(size_t n, int packets, usb_transfer_t **out) {
    *out = calloc(1, sizeof(**out));
    (*out)->data_buffer = calloc(1, n);
    (*out)->data_buffer_size = n;
    (*out)->num_isoc_packets = packets;
    return ESP_OK;
}
static inline int usb_host_transfer_free(usb_transfer_t *t) {
    free(t->data_buffer); free(t); return ESP_OK;
}
static inline int usb_host_interface_claim(void *c, void *d, unsigned i, unsigned a) {
    (void)c; (void)d; (void)i; (void)a; return ESP_OK;
}
static inline int usb_host_interface_release(void *c, void *d, unsigned i) {
    (void)c; (void)d; (void)i; return ESP_OK;
}
static inline int usb_host_endpoint_halt(void *d, unsigned e) {
    (void)d; (void)e; return ESP_OK;
}
static inline int usb_host_endpoint_flush(void *d, unsigned e) {
    (void)d; (void)e; return ESP_OK;
}
