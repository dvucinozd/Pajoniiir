#include "usb_media_partition.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void put_u16le(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xffu);
    p[1] = (uint8_t)(v >> 8);
}

static void put_u32le(uint8_t *p, uint32_t v)
{
    put_u16le(p, (uint16_t)(v & 0xffffu));
    put_u16le(p + 2, (uint16_t)(v >> 16));
}

static void put_u64le(uint8_t *p, uint64_t v)
{
    put_u32le(p, (uint32_t)(v & 0xffffffffull));
    put_u32le(p + 4, (uint32_t)(v >> 32));
}

static void make_exfat_boot(uint8_t sector[512])
{
    memset(sector, 0, 512);
    sector[0] = 0xeb;
    sector[1] = 0x76;
    sector[2] = 0x90;
    memcpy(sector + 3, "EXFAT   ", 8);
    put_u16le(sector + 510, 0xaa55u);
}

static void make_fat32_boot(uint8_t sector[512])
{
    memset(sector, 0, 512);
    sector[0] = 0xeb;
    sector[1] = 0x58;
    sector[2] = 0x90;
    memcpy(sector + 82, "FAT32   ", 8);
    put_u16le(sector + 510, 0xaa55u);
}

static void make_mbr(uint8_t sector[512], uint8_t type, uint32_t first_lba, uint32_t sectors)
{
    memset(sector, 0, 512);
    uint8_t *p = sector + 446;
    p[4] = type;
    put_u32le(p + 8, first_lba);
    put_u32le(p + 12, sectors);
    put_u16le(sector + 510, 0xaa55u);
}

static void make_protective_mbr(uint8_t sector[512])
{
    make_mbr(sector, 0xeeu, 1u, 0xffffffffu);
}

static void make_gpt_header(uint8_t sector[512],
                            uint64_t entries_lba,
                            uint32_t entry_count,
                            uint32_t entry_size)
{
    memset(sector, 0, 512);
    memcpy(sector, "EFI PART", 8);
    put_u32le(sector + 12, 92u);
    put_u64le(sector + 72, entries_lba);
    put_u32le(sector + 80, entry_count);
    put_u32le(sector + 84, entry_size);
}

static void make_gpt_basic_data_entry(uint8_t entry[128], uint64_t first_lba, uint64_t last_lba)
{
    static const uint8_t microsoft_basic_data_guid[16] = {
        0xa2, 0xa0, 0xd0, 0xeb, 0xe5, 0xb9, 0x33, 0x44,
        0x87, 0xc0, 0x68, 0xb6, 0xb7, 0x26, 0x99, 0xc7,
    };
    memset(entry, 0, 128);
    memcpy(entry, microsoft_basic_data_guid, sizeof(microsoft_basic_data_guid));
    put_u64le(entry + 32, first_lba);
    put_u64le(entry + 40, last_lba);
}

static void test_exfat_superfloppy(void)
{
    uint8_t sector[512];
    make_exfat_boot(sector);

    usb_media_layout_t layout;
    assert(usb_media_partition_scan_mbr_or_sfd(sector, 512u, &layout) == USB_MEDIA_PARTITION_OK);
    assert(layout.count == 1u);
    assert(layout.candidates[0].first_lba == 0u);
    assert(layout.candidates[0].kind == USB_MEDIA_VOLUME_EXFAT);
}

static void test_fat32_mbr_candidate(void)
{
    uint8_t sector[512];
    make_mbr(sector, 0x0cu, 2048u, 65536u);

    usb_media_layout_t layout;
    assert(usb_media_partition_scan_mbr_or_sfd(sector, 512u, &layout) == USB_MEDIA_PARTITION_OK);
    assert(layout.count == 1u);
    assert(layout.candidates[0].first_lba == 2048u);
    assert(layout.candidates[0].kind == USB_MEDIA_VOLUME_FAT);
}

static void test_exfat_mbr_candidate_type_07(void)
{
    uint8_t sector[512];
    make_mbr(sector, 0x07u, 4096u, 131072u);

    usb_media_layout_t layout;
    assert(usb_media_partition_scan_mbr_or_sfd(sector, 512u, &layout) == USB_MEDIA_PARTITION_OK);
    assert(layout.count == 1u);
    assert(layout.candidates[0].first_lba == 4096u);
    assert(layout.candidates[0].kind == USB_MEDIA_VOLUME_UNKNOWN);
}

static void test_gpt_basic_data_candidate(void)
{
    uint8_t mbr[512];
    uint8_t header[512];
    uint8_t entries[512];
    make_protective_mbr(mbr);
    make_gpt_header(header, 2u, 4u, 128u);
    memset(entries, 0, sizeof(entries));
    make_gpt_basic_data_entry(entries, 32768u, 98303u);

    usb_media_layout_t layout;
    assert(usb_media_partition_scan_mbr_or_sfd(mbr, 512u, &layout) == USB_MEDIA_PARTITION_NEEDS_GPT);
    assert(usb_media_partition_scan_gpt(header, entries, sizeof(entries), &layout) == USB_MEDIA_PARTITION_OK);
    assert(layout.count == 1u);
    assert(layout.candidates[0].first_lba == 32768u);
    assert(layout.candidates[0].sector_count == 65536u);
    assert(layout.candidates[0].kind == USB_MEDIA_VOLUME_UNKNOWN);
}

static void test_reject_malformed_gpt(void)
{
    uint8_t header[512] = {0};
    uint8_t entries[512] = {0};
    usb_media_layout_t layout;
    assert(usb_media_partition_scan_gpt(header, entries, sizeof(entries), &layout) ==
           USB_MEDIA_PARTITION_INVALID);
}

static void test_boot_sector_classification(void)
{
    uint8_t exfat[512];
    uint8_t fat32[512];
    uint8_t blank[512] = {0};
    make_exfat_boot(exfat);
    make_fat32_boot(fat32);
    assert(usb_media_partition_classify_boot_sector(exfat, 512u) == USB_MEDIA_VOLUME_EXFAT);
    assert(usb_media_partition_classify_boot_sector(fat32, 512u) == USB_MEDIA_VOLUME_FAT);
    assert(usb_media_partition_classify_boot_sector(blank, 512u) == USB_MEDIA_VOLUME_UNKNOWN);
}

int main(void)
{
    test_exfat_superfloppy();
    test_fat32_mbr_candidate();
    test_exfat_mbr_candidate_type_07();
    test_gpt_basic_data_candidate();
    test_reject_malformed_gpt();
    test_boot_sector_classification();
    puts("usb_media_partition tests passed");
    return 0;
}
