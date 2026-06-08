#pragma once

// USB Host mass-storage support for the Rekordbox music drive.
//
// Brings up the ESP32-P4 USB host stack + MSC class driver and mounts the
// connected flash drive at "/usb" via FATFS. When the drive appears or goes
// away, the registered callback fires so the app can (re)load the library and
// refresh the UI. The drive plugs into the board's High-Speed USB-C port.

#include <stdbool.h>
#include "esp_err.h"

#define USB_STORAGE_MOUNT_POINT  "/usb"

// Fired from the USB storage task when the drive mounts (true) or unmounts
// (false). Runs in its own task context — callers that touch LVGL must take the
// UI lock themselves.
typedef void (*usb_storage_event_cb_t)(bool mounted);

// Start the USB host + MSC stack. Non-blocking: enumeration/mount happen later
// on a background task. `cb` may be NULL.
esp_err_t usb_storage_init(usb_storage_event_cb_t cb);

// Whether a drive is currently mounted at /usb.
bool usb_storage_is_mounted(void);
