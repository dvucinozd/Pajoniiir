#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_vfs_fat.h"
#include "usb/msc_host.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct usb_media_mount usb_media_mount_t;

typedef struct {
    uint32_t base_lba;
    uint32_t sector_count;
    uint32_t sector_size;
    bool exfat;
    bool gpt;
} usb_media_mount_info_t;

esp_err_t usb_media_mount(msc_host_device_handle_t device,
                          const char *base_path,
                          const esp_vfs_fat_mount_config_t *mount_config,
                          usb_media_mount_t **out_mount);

esp_err_t usb_media_unmount(usb_media_mount_t *mount);

bool usb_media_mount_get_info(const usb_media_mount_t *mount,
                              usb_media_mount_info_t *out_info);

#ifdef __cplusplus
}
#endif
