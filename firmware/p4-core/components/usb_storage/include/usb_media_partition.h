#pragma once

#include <stddef.h>
#include <stdint.h>

#define USB_MEDIA_MAX_CANDIDATES 8u
#define USB_MEDIA_SECTOR_SIZE_MIN 512u

typedef enum {
    USB_MEDIA_VOLUME_UNKNOWN = 0,
    USB_MEDIA_VOLUME_FAT,
    USB_MEDIA_VOLUME_EXFAT,
} usb_media_volume_kind_t;

typedef enum {
    USB_MEDIA_PARTITION_OK = 0,
    USB_MEDIA_PARTITION_INVALID,
    USB_MEDIA_PARTITION_NEEDS_GPT,
    USB_MEDIA_PARTITION_NO_CANDIDATE,
} usb_media_partition_result_t;

typedef struct {
    uint32_t first_lba;
    uint32_t sector_count;
    usb_media_volume_kind_t kind;
} usb_media_candidate_t;

typedef struct {
    usb_media_candidate_t candidates[USB_MEDIA_MAX_CANDIDATES];
    size_t count;
    int protective_mbr;
} usb_media_layout_t;

usb_media_volume_kind_t usb_media_partition_classify_boot_sector(const uint8_t *sector,
                                                                 size_t sector_size);

usb_media_partition_result_t usb_media_partition_scan_mbr_or_sfd(const uint8_t *sector,
                                                                 size_t sector_size,
                                                                 usb_media_layout_t *layout);

// `header_sector` must point to at least USB_MEDIA_SECTOR_SIZE_MIN bytes.
usb_media_partition_result_t usb_media_partition_scan_gpt(const uint8_t *header_sector,
                                                          const uint8_t *entries,
                                                          size_t entries_size,
                                                          usb_media_layout_t *layout);

int usb_media_partition_append_candidate(usb_media_layout_t *layout,
                                         uint32_t first_lba,
                                         uint32_t sector_count,
                                         usb_media_volume_kind_t kind);
