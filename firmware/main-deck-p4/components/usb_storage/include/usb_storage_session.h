#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool connected;
    bool mounted;
    uint8_t dev_addr;
    uintptr_t accepted_handle;
    uint32_t epoch;
} usb_storage_session_t;

typedef enum {
    USB_STORAGE_CONNECT_ACCEPTED,
    USB_STORAGE_CONNECT_DUPLICATE,
    USB_STORAGE_CONNECT_IGNORED_SECONDARY,
} usb_storage_connect_result_t;

typedef enum {
    USB_STORAGE_DISCONNECT_ACCEPTED,
    USB_STORAGE_DISCONNECT_IGNORED_FOREIGN,
    USB_STORAGE_DISCONNECT_ALREADY_INACTIVE,
} usb_storage_disconnect_result_t;

void usb_storage_session_reset(usb_storage_session_t *session);

usb_storage_connect_result_t usb_storage_session_on_connect(
    usb_storage_session_t *session,
    uint8_t dev_addr);

bool usb_storage_session_bind_handle(usb_storage_session_t *session,
                                     uint32_t epoch,
                                     uint8_t dev_addr,
                                     uintptr_t handle);

void usb_storage_session_release_handle(usb_storage_session_t *session,
                                        uintptr_t handle);

usb_storage_disconnect_result_t usb_storage_session_on_disconnect(
    usb_storage_session_t *session,
    uintptr_t handle);

bool usb_storage_session_matches(const usb_storage_session_t *session,
                                 uint32_t epoch,
                                 uint8_t dev_addr);

bool usb_storage_session_commit_mounted(usb_storage_session_t *session,
                                        uint32_t epoch,
                                        uint8_t dev_addr);

void usb_storage_session_mark_unmounted(usb_storage_session_t *session);
