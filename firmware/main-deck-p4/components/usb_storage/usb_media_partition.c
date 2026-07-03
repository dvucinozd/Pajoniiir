#include "usb_media_partition.h"

#include <limits.h>
#include <string.h>

static uint16_t read_u16le(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_u32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint64_t read_u64le(const uint8_t *p)
{
    return (uint64_t)read_u32le(p) | ((uint64_t)read_u32le(p + 4) << 32);
}

static int has_boot_signature(const uint8_t *sector, size_t sector_size)
{
    return sector != NULL && sector_size >= USB_MEDIA_SECTOR_SIZE_MIN &&
           read_u16le(sector + 510) == 0xaa55u;
}

static int has_valid_boot_jump(const uint8_t *sector)
{
    return sector[0] == 0xebu || sector[0] == 0xe9u;
}

static int is_mbr_fat_type(uint8_t type)
{
    return type == 0x01u || type == 0x04u || type == 0x06u || type == 0x0bu ||
           type == 0x0cu || type == 0x0eu;
}

static int lba_range_fits_u32(uint32_t first_lba, uint32_t sector_count)
{
    const uint64_t end_lba = (uint64_t)first_lba + (uint64_t)sector_count - 1u;
    return end_lba <= UINT32_MAX;
}

static void layout_clear(usb_media_layout_t *layout)
{
    memset(layout, 0, sizeof(*layout));
}

usb_media_volume_kind_t usb_media_partition_classify_boot_sector(const uint8_t *sector,
                                                                 size_t sector_size)
{
    if (!has_boot_signature(sector, sector_size) || !has_valid_boot_jump(sector)) {
        return USB_MEDIA_VOLUME_UNKNOWN;
    }

    if (memcmp(sector + 3, "EXFAT   ", 8) == 0) {
        return USB_MEDIA_VOLUME_EXFAT;
    }

    if (memcmp(sector + 54, "FAT", 3) == 0 || memcmp(sector + 82, "FAT32", 5) == 0) {
        return USB_MEDIA_VOLUME_FAT;
    }

    return USB_MEDIA_VOLUME_UNKNOWN;
}

int usb_media_partition_append_candidate(usb_media_layout_t *layout,
                                         uint32_t first_lba,
                                         uint32_t sector_count,
                                         usb_media_volume_kind_t kind)
{
    if (layout == NULL || sector_count == 0u) {
        return 0;
    }

    for (size_t i = 0; i < layout->count; ++i) {
        usb_media_candidate_t *candidate = &layout->candidates[i];
        if (candidate->first_lba == first_lba) {
            if (candidate->kind == USB_MEDIA_VOLUME_UNKNOWN && kind != USB_MEDIA_VOLUME_UNKNOWN) {
                candidate->kind = kind;
            }
            return 1;
        }
    }

    if (layout->count >= USB_MEDIA_MAX_CANDIDATES) {
        return 0;
    }

    usb_media_candidate_t *candidate = &layout->candidates[layout->count++];
    candidate->first_lba = first_lba;
    candidate->sector_count = sector_count;
    candidate->kind = kind;
    return 1;
}

usb_media_partition_result_t usb_media_partition_scan_mbr_or_sfd(const uint8_t *sector,
                                                                 size_t sector_size,
                                                                 usb_media_layout_t *layout)
{
    if (layout == NULL) {
        return USB_MEDIA_PARTITION_INVALID;
    }
    layout_clear(layout);

    if (!has_boot_signature(sector, sector_size)) {
        return USB_MEDIA_PARTITION_INVALID;
    }

    const usb_media_volume_kind_t sfd_kind =
        usb_media_partition_classify_boot_sector(sector, sector_size);
    if (sfd_kind != USB_MEDIA_VOLUME_UNKNOWN) {
        return usb_media_partition_append_candidate(layout, 0u, UINT32_MAX, sfd_kind)
                   ? USB_MEDIA_PARTITION_OK
                   : USB_MEDIA_PARTITION_INVALID;
    }

    for (size_t i = 0; i < 4u; ++i) {
        const uint8_t *entry = sector + 446u + (i * 16u);
        const uint8_t type = entry[4];
        const uint32_t first_lba = read_u32le(entry + 8);
        const uint32_t sector_count = read_u32le(entry + 12);

        if (type == 0x00u) {
            continue;
        }

        if (type == 0xeeu) {
            layout->protective_mbr = 1;
            continue;
        }

        if (first_lba == 0u || sector_count == 0u) {
            continue;
        }

        if (!lba_range_fits_u32(first_lba, sector_count)) {
            continue;
        }

        if (is_mbr_fat_type(type)) {
            (void)usb_media_partition_append_candidate(layout, first_lba, sector_count,
                                                       USB_MEDIA_VOLUME_FAT);
        } else if (type == 0x07u) {
            (void)usb_media_partition_append_candidate(layout, first_lba, sector_count,
                                                       USB_MEDIA_VOLUME_UNKNOWN);
        }
    }

    if (layout->protective_mbr) {
        return USB_MEDIA_PARTITION_NEEDS_GPT;
    }

    return layout->count > 0u ? USB_MEDIA_PARTITION_OK : USB_MEDIA_PARTITION_NO_CANDIDATE;
}

usb_media_partition_result_t usb_media_partition_scan_gpt(const uint8_t *header_sector,
                                                          const uint8_t *entries,
                                                          size_t entries_size,
                                                          usb_media_layout_t *layout)
{
    static const uint8_t microsoft_basic_data_guid[16] = {
        0xa2, 0xa0, 0xd0, 0xeb, 0xe5, 0xb9, 0x33, 0x44,
        0x87, 0xc0, 0x68, 0xb6, 0xb7, 0x26, 0x99, 0xc7,
    };

    if (header_sector == NULL || entries == NULL || layout == NULL) {
        return USB_MEDIA_PARTITION_INVALID;
    }
    layout_clear(layout);

    if (memcmp(header_sector, "EFI PART", 8) != 0) {
        return USB_MEDIA_PARTITION_INVALID;
    }

    const uint32_t header_size = read_u32le(header_sector + 12);
    const uint32_t entry_count = read_u32le(header_sector + 80);
    const uint32_t entry_size = read_u32le(header_sector + 84);
    if (header_size < 92u || header_size > USB_MEDIA_SECTOR_SIZE_MIN || entry_count == 0u ||
        entry_size < 128u || entry_size > 512u) {
        return USB_MEDIA_PARTITION_INVALID;
    }

    const size_t available_entries = entries_size / entry_size;
    const size_t entries_to_scan =
        available_entries < (size_t)entry_count ? available_entries : (size_t)entry_count;

    for (size_t i = 0; i < entries_to_scan; ++i) {
        const uint8_t *entry = entries + (i * entry_size);
        if (memcmp(entry, microsoft_basic_data_guid, sizeof(microsoft_basic_data_guid)) != 0) {
            continue;
        }

        const uint64_t first_lba = read_u64le(entry + 32);
        const uint64_t last_lba = read_u64le(entry + 40);
        if (first_lba == 0u || last_lba < first_lba || first_lba > UINT32_MAX ||
            last_lba > UINT32_MAX) {
            continue;
        }

        const uint64_t sector_count = last_lba - first_lba + 1u;
        if (sector_count == 0u || sector_count > UINT32_MAX) {
            continue;
        }

        (void)usb_media_partition_append_candidate(layout, (uint32_t)first_lba,
                                                   (uint32_t)sector_count,
                                                   USB_MEDIA_VOLUME_UNKNOWN);
    }

    return layout->count > 0u ? USB_MEDIA_PARTITION_OK : USB_MEDIA_PARTITION_NO_CANDIDATE;
}
