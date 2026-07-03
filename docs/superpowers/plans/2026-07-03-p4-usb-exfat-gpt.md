# P4 USB exFAT And GPT Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Mount rekordbox USB drives on ESP32-P4 when they are FAT32 or exFAT, with either MBR, GPT, or superfloppy layout, while keeping the public media path `/usb`.

**Architecture:** Add a small P4 USB media mount layer that discovers a FAT/exFAT volume start LBA, registers a translated FatFs diskio backend, and mounts that logical volume at `/usb`. Add a repo-local P4 FatFs component override copied from ESP-IDF v5.5 with `FF_FS_EXFAT=1`, then route existing `usb_storage.c` through the new mount layer instead of `msc_host_vfs_register()`.

**Tech Stack:** ESP-IDF v5.5, ESP32-P4, `espressif/usb_host_msc`, FatFs, C99 host tests with GCC/PowerShell, P4 `usb_storage` component, existing `media_io_gate`.

---

## File Structure

- Create: `firmware/main-deck-p4/components/usb_storage/include/usb_media_partition.h`
  - Pure partition/boot-sector parser API; no ESP-IDF or USB dependency beyond integer types.
- Create: `firmware/main-deck-p4/components/usb_storage/usb_media_partition.c`
  - Detects exFAT/FAT boot sectors, MBR partitions, GPT Microsoft Basic Data partitions, and candidate ordering.
- Create: `firmware/main-deck-p4/components/usb_storage/include/usb_media_mount.h`
  - Runtime mount API used by `usb_storage.c`.
- Create: `firmware/main-deck-p4/components/usb_storage/usb_media_mount.c`
  - MSC sector reads, candidate probing, translated diskio registration, VFS registration, mount/unmount cleanup.
- Modify: `firmware/main-deck-p4/components/usb_storage/usb_storage.c`
  - Replaces direct `msc_host_vfs_register()`/`msc_host_vfs_unregister()` with `usb_media_mount()`/`usb_media_unmount()`.
- Modify: `firmware/main-deck-p4/components/usb_storage/CMakeLists.txt`
  - Adds new sources and required private/public dependencies.
- Create: `firmware/main-deck-p4/components/fatfs/`
  - Repo-local FatFs override copied from `C:\Espressif\frameworks\esp-idf-v5.5\components\fatfs`.
- Modify: `firmware/main-deck-p4/components/fatfs/src/ffconf.h`
  - Changes `FF_FS_EXFAT` from `0` to `1`; documents upstream source.
- Create: `tests/usb_media_partition/test_usb_media_partition.c`
  - Host tests for pure parser behavior.
- Create: `tests/usb_media_partition/Makefile`
  - Local build command for parser host tests.
- Modify: `tests/run_p4_host_tests.ps1`
  - Adds parser host test to the P4 host suite.
- Modify: `docs/bench-notes.md`
  - Updates open item wording after implementation and records hardware smoke cases.

## Task 1: Pure Partition Parser Tests

**Files:**
- Create: `tests/usb_media_partition/test_usb_media_partition.c`
- Create: `tests/usb_media_partition/Makefile`
- Modify: `tests/run_p4_host_tests.ps1`

- [ ] **Step 1: Create the host test directory and Makefile**

Create `tests/usb_media_partition/Makefile`:

```make
CC      = gcc
CFLAGS  = -Wall -Wextra -Wpedantic -Werror=implicit-function-declaration -std=c99
INCS    = -I../../firmware/main-deck-p4/components/usb_storage/include
SRCS    = test_usb_media_partition.c \
          ../../firmware/main-deck-p4/components/usb_storage/usb_media_partition.c
TARGET  = test_usb_media_partition

.PHONY: all test clean

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(INCS) -o $(TARGET) $(SRCS)

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) test_usb_media_partition.exe
```

- [ ] **Step 2: Write failing parser tests**

Create `tests/usb_media_partition/test_usb_media_partition.c`:

```c
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

static void make_gpt_header(uint8_t sector[512], uint64_t entries_lba, uint32_t entry_count, uint32_t entry_size)
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
```

- [ ] **Step 3: Add the failing host test to the P4 runner**

In `tests/run_p4_host_tests.ps1`, add this entry to the `$tests = @(` list near the other small component tests:

```powershell
    @{
        Name = "usb_media_partition"
        Dir = "tests/usb_media_partition"
        Target = "test_usb_media_partition.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-Werror=implicit-function-declaration", "-std=c99",
            "-I../../firmware/main-deck-p4/components/usb_storage/include",
            "-o", "test_usb_media_partition.exe",
            "test_usb_media_partition.c",
            "../../firmware/main-deck-p4/components/usb_storage/usb_media_partition.c"
        )
    },
```

- [ ] **Step 4: Run the new test and verify RED**

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
.\tests\run_p4_host_tests.ps1
```

Expected: build fails for `usb_media_partition` because `usb_media_partition.h` and `usb_media_partition.c` do not exist.

- [ ] **Step 5: Commit the RED test**

```powershell
git add tests/usb_media_partition tests/run_p4_host_tests.ps1
git commit -m "test: add usb media partition parser coverage"
```

## Task 2: Partition Parser Implementation

**Files:**
- Create: `firmware/main-deck-p4/components/usb_storage/include/usb_media_partition.h`
- Create: `firmware/main-deck-p4/components/usb_storage/usb_media_partition.c`

- [ ] **Step 1: Add the parser public header**

Create `firmware/main-deck-p4/components/usb_storage/include/usb_media_partition.h`:

```c
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

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

usb_media_partition_result_t usb_media_partition_scan_mbr_or_sfd(const uint8_t *sector0,
                                                                 size_t sector_size,
                                                                 usb_media_layout_t *out);

usb_media_partition_result_t usb_media_partition_scan_gpt(const uint8_t *gpt_header,
                                                          const uint8_t *gpt_entries,
                                                          size_t gpt_entries_bytes,
                                                          usb_media_layout_t *out);

int usb_media_partition_append_candidate(usb_media_layout_t *layout,
                                         uint32_t first_lba,
                                         uint32_t sector_count,
                                         usb_media_volume_kind_t kind);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 2: Implement the parser**

Create `firmware/main-deck-p4/components/usb_storage/usb_media_partition.c`:

```c
#include "usb_media_partition.h"

#include <stdbool.h>
#include <string.h>

static uint16_t rd_u16le(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd_u32le(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint64_t rd_u64le(const uint8_t *p)
{
    return (uint64_t)rd_u32le(p) | ((uint64_t)rd_u32le(p + 4) << 32);
}

static bool has_boot_signature(const uint8_t *sector, size_t sector_size)
{
    return sector && sector_size >= USB_MEDIA_SECTOR_SIZE_MIN &&
           rd_u16le(sector + 510) == 0xaa55u;
}

static bool has_jump_boot(const uint8_t *sector)
{
    return sector[0] == 0xeb || sector[0] == 0xe9;
}

usb_media_volume_kind_t usb_media_partition_classify_boot_sector(const uint8_t *sector,
                                                                 size_t sector_size)
{
    if (!has_boot_signature(sector, sector_size) || !has_jump_boot(sector)) {
        return USB_MEDIA_VOLUME_UNKNOWN;
    }
    if (memcmp(sector + 3, "EXFAT   ", 8) == 0) {
        return USB_MEDIA_VOLUME_EXFAT;
    }
    if (memcmp(sector + 54, "FAT", 3) == 0 ||
        memcmp(sector + 82, "FAT32", 5) == 0) {
        return USB_MEDIA_VOLUME_FAT;
    }
    return USB_MEDIA_VOLUME_UNKNOWN;
}

int usb_media_partition_append_candidate(usb_media_layout_t *layout,
                                         uint32_t first_lba,
                                         uint32_t sector_count,
                                         usb_media_volume_kind_t kind)
{
    if (!layout || sector_count == 0u || layout->count >= USB_MEDIA_MAX_CANDIDATES) {
        return 0;
    }
    for (size_t i = 0; i < layout->count; i++) {
        if (layout->candidates[i].first_lba == first_lba) {
            if (layout->candidates[i].kind == USB_MEDIA_VOLUME_UNKNOWN) {
                layout->candidates[i].kind = kind;
            }
            return 1;
        }
    }
    layout->candidates[layout->count++] = (usb_media_candidate_t) {
        .first_lba = first_lba,
        .sector_count = sector_count,
        .kind = kind,
    };
    return 1;
}

static bool mbr_type_is_candidate(uint8_t type)
{
    switch (type) {
    case 0x01:
    case 0x04:
    case 0x06:
    case 0x0b:
    case 0x0c:
    case 0x0e:
    case 0x07:
        return true;
    default:
        return false;
    }
}

static usb_media_volume_kind_t mbr_type_hint(uint8_t type)
{
    return (type == 0x07u) ? USB_MEDIA_VOLUME_UNKNOWN : USB_MEDIA_VOLUME_FAT;
}

usb_media_partition_result_t usb_media_partition_scan_mbr_or_sfd(const uint8_t *sector0,
                                                                 size_t sector_size,
                                                                 usb_media_layout_t *out)
{
    if (!sector0 || sector_size < USB_MEDIA_SECTOR_SIZE_MIN || !out) {
        return USB_MEDIA_PARTITION_INVALID;
    }
    memset(out, 0, sizeof(*out));

    usb_media_volume_kind_t sfd = usb_media_partition_classify_boot_sector(sector0, sector_size);
    if (sfd != USB_MEDIA_VOLUME_UNKNOWN) {
        usb_media_partition_append_candidate(out, 0u, UINT32_MAX, sfd);
        return USB_MEDIA_PARTITION_OK;
    }

    if (!has_boot_signature(sector0, sector_size)) {
        return USB_MEDIA_PARTITION_INVALID;
    }

    bool saw_protective = false;
    for (size_t i = 0; i < 4u; i++) {
        const uint8_t *entry = sector0 + 446u + (i * 16u);
        uint8_t type = entry[4];
        uint32_t first_lba = rd_u32le(entry + 8);
        uint32_t sector_count = rd_u32le(entry + 12);
        if (type == 0xeeu) {
            saw_protective = true;
            continue;
        }
        if (mbr_type_is_candidate(type)) {
            usb_media_partition_append_candidate(out,
                                                 first_lba,
                                                 sector_count,
                                                 mbr_type_hint(type));
        }
    }
    if (saw_protective) {
        out->protective_mbr = 1;
        return USB_MEDIA_PARTITION_NEEDS_GPT;
    }
    return out->count > 0u ? USB_MEDIA_PARTITION_OK : USB_MEDIA_PARTITION_NO_CANDIDATE;
}

static bool guid_is_microsoft_basic_data(const uint8_t guid[16])
{
    static const uint8_t microsoft_basic_data_guid[16] = {
        0xa2, 0xa0, 0xd0, 0xeb, 0xe5, 0xb9, 0x33, 0x44,
        0x87, 0xc0, 0x68, 0xb6, 0xb7, 0x26, 0x99, 0xc7,
    };
    return memcmp(guid, microsoft_basic_data_guid, sizeof(microsoft_basic_data_guid)) == 0;
}

usb_media_partition_result_t usb_media_partition_scan_gpt(const uint8_t *gpt_header,
                                                          const uint8_t *gpt_entries,
                                                          size_t gpt_entries_bytes,
                                                          usb_media_layout_t *out)
{
    if (!gpt_header || !gpt_entries || !out ||
        memcmp(gpt_header, "EFI PART", 8) != 0) {
        return USB_MEDIA_PARTITION_INVALID;
    }

    uint32_t header_size = rd_u32le(gpt_header + 12);
    uint32_t entry_count = rd_u32le(gpt_header + 80);
    uint32_t entry_size = rd_u32le(gpt_header + 84);
    if (header_size < 92u || entry_size < 128u || entry_size > 512u || entry_count == 0u) {
        return USB_MEDIA_PARTITION_INVALID;
    }

    memset(out, 0, sizeof(*out));
    size_t max_entries_by_buffer = gpt_entries_bytes / entry_size;
    size_t entries = entry_count < max_entries_by_buffer ? entry_count : max_entries_by_buffer;
    for (size_t i = 0; i < entries; i++) {
        const uint8_t *entry = gpt_entries + (i * entry_size);
        uint64_t first_lba = rd_u64le(entry + 32);
        uint64_t last_lba = rd_u64le(entry + 40);
        if (!guid_is_microsoft_basic_data(entry) || first_lba == 0u || last_lba < first_lba) {
            continue;
        }
        uint64_t count64 = last_lba - first_lba + 1u;
        if (first_lba > UINT32_MAX || count64 > UINT32_MAX) {
            continue;
        }
        usb_media_partition_append_candidate(out,
                                             (uint32_t)first_lba,
                                             (uint32_t)count64,
                                             USB_MEDIA_VOLUME_UNKNOWN);
    }
    return out->count > 0u ? USB_MEDIA_PARTITION_OK : USB_MEDIA_PARTITION_NO_CANDIDATE;
}
```

- [ ] **Step 3: Run parser host tests and verify GREEN**

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
.\tests\run_p4_host_tests.ps1
```

Expected: `usb_media_partition tests passed` appears and the full P4 host suite ends with `P4 host tests passed.`

- [ ] **Step 4: Commit parser implementation**

```powershell
git add firmware/main-deck-p4/components/usb_storage/include/usb_media_partition.h firmware/main-deck-p4/components/usb_storage/usb_media_partition.c
git commit -m "feat: parse usb media fat exfat partitions"
```

## Task 3: Runtime Mount API And Translated DiskIO

**Files:**
- Create: `firmware/main-deck-p4/components/usb_storage/include/usb_media_mount.h`
- Create: `firmware/main-deck-p4/components/usb_storage/usb_media_mount.c`
- Modify: `firmware/main-deck-p4/components/usb_storage/CMakeLists.txt`

- [ ] **Step 1: Add the mount API header**

Create `firmware/main-deck-p4/components/usb_storage/include/usb_media_mount.h`:

```c
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
```

- [ ] **Step 2: Add the translated diskio implementation**

Create `firmware/main-deck-p4/components/usb_storage/usb_media_mount.c` with this implementation:

```c
#include "usb_media_mount.h"

#include "diskio_impl.h"
#include "esp_log.h"
#include "esp_private/msc_scsi_bot.h"
#include "esp_vfs_fat.h"
#include "ff.h"
#include "usb_media_partition.h"
#include "usb/msc_host.h"

#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define USB_MEDIA_MAX_MOUNTS 4u
#define USB_MEDIA_DRIVE_STR_LEN 3u
#define USB_MEDIA_GPT_ENTRY_READ_BYTES 4096u

static const char *TAG = "usb_media_mount";

struct usb_media_mount {
    FATFS *fs;
    char drive[USB_MEDIA_DRIVE_STR_LEN];
    char *base_path;
    BYTE pdrv;
    msc_host_device_handle_t device;
    uint32_t base_lba;
    uint32_t sector_count;
    uint32_t sector_size;
    bool exfat;
    bool gpt;
};

static usb_media_mount_t *s_mounts[USB_MEDIA_MAX_MOUNTS];

static uint16_t rd_u16le(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd_u32le(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static DSTATUS translated_initialize(BYTE pdrv)
{
    (void)pdrv;
    return RES_OK;
}

static DSTATUS translated_status(BYTE pdrv)
{
    return pdrv < USB_MEDIA_MAX_MOUNTS && s_mounts[pdrv] ? RES_OK : STA_NOINIT;
}

static DRESULT translated_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count)
{
    if (pdrv >= USB_MEDIA_MAX_MOUNTS || !s_mounts[pdrv] || !buff || count == 0u) {
        return RES_PARERR;
    }
    usb_media_mount_t *mount = s_mounts[pdrv];
    uint64_t end = (uint64_t)sector + (uint64_t)count;
    if (end > mount->sector_count) {
        return RES_PARERR;
    }
    esp_err_t rc = scsi_cmd_read10(mount->device,
                                   buff,
                                   mount->base_lba + sector,
                                   count,
                                   mount->sector_size);
    return rc == ESP_OK ? RES_OK : RES_ERROR;
}

static DRESULT translated_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count)
{
    if (pdrv >= USB_MEDIA_MAX_MOUNTS || !s_mounts[pdrv] || !buff || count == 0u) {
        return RES_PARERR;
    }
    usb_media_mount_t *mount = s_mounts[pdrv];
    uint64_t end = (uint64_t)sector + (uint64_t)count;
    if (end > mount->sector_count) {
        return RES_PARERR;
    }
    esp_err_t rc = scsi_cmd_write10(mount->device,
                                    buff,
                                    mount->base_lba + sector,
                                    count,
                                    mount->sector_size);
    return rc == ESP_OK ? RES_OK : RES_ERROR;
}

static DRESULT translated_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if (pdrv >= USB_MEDIA_MAX_MOUNTS || !s_mounts[pdrv]) {
        return RES_PARERR;
    }
    usb_media_mount_t *mount = s_mounts[pdrv];
    switch (cmd) {
    case CTRL_SYNC:
        return RES_OK;
    case GET_SECTOR_COUNT:
        *(DWORD *)buff = mount->sector_count;
        return RES_OK;
    case GET_SECTOR_SIZE:
        *(WORD *)buff = (WORD)mount->sector_size;
        return RES_OK;
    case GET_BLOCK_SIZE:
        return RES_ERROR;
    default:
        return RES_PARERR;
    }
}

static const ff_diskio_impl_t s_translated_diskio = {
    .init = translated_initialize,
    .status = translated_status,
    .read = translated_read,
    .write = translated_write,
    .ioctl = translated_ioctl,
};

static esp_err_t read_sector(msc_host_device_handle_t device,
                             uint32_t sector,
                             uint32_t sector_size,
                             void *buf)
{
    return scsi_cmd_read10(device, buf, sector, 1u, sector_size);
}

static esp_err_t discover_layout(msc_host_device_handle_t device,
                                 uint32_t sector_size,
                                 usb_media_layout_t *layout,
                                 bool *out_gpt)
{
    uint8_t *sector0 = calloc(1, sector_size);
    uint8_t *gpt_header = calloc(1, sector_size);
    uint8_t *gpt_entries = calloc(1, USB_MEDIA_GPT_ENTRY_READ_BYTES);
    if (!sector0 || !gpt_header || !gpt_entries) {
        free(sector0);
        free(gpt_header);
        free(gpt_entries);
        return ESP_ERR_NO_MEM;
    }

    esp_err_t rc = read_sector(device, 0u, sector_size, sector0);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "read sector 0 failed: %s", esp_err_to_name(rc));
        free(sector0);
        free(gpt_header);
        free(gpt_entries);
        return rc;
    }

    usb_media_partition_result_t pr = usb_media_partition_scan_mbr_or_sfd(sector0, sector_size, layout);
    if (pr == USB_MEDIA_PARTITION_NEEDS_GPT) {
        rc = read_sector(device, 1u, sector_size, gpt_header);
        if (rc != ESP_OK) {
            ESP_LOGE(TAG, "read GPT header failed: %s", esp_err_to_name(rc));
            free(sector0);
            free(gpt_header);
            free(gpt_entries);
            return rc;
        }
        uint64_t entries_lba = (uint64_t)rd_u32le(gpt_header + 72) |
                               ((uint64_t)rd_u32le(gpt_header + 76) << 32);
        uint32_t entry_size = rd_u32le(gpt_header + 84);
        uint32_t sectors_to_read = USB_MEDIA_GPT_ENTRY_READ_BYTES / sector_size;
        if (entry_size < 128u || sectors_to_read == 0u || entries_lba > UINT32_MAX) {
            free(sector0);
            free(gpt_header);
            free(gpt_entries);
            return ESP_ERR_INVALID_SIZE;
        }
        for (uint32_t i = 0; i < sectors_to_read; i++) {
            rc = read_sector(device, (uint32_t)entries_lba + i, sector_size,
                             gpt_entries + (i * sector_size));
            if (rc != ESP_OK) {
                ESP_LOGE(TAG, "read GPT entries failed: %s", esp_err_to_name(rc));
                free(sector0);
                free(gpt_header);
                free(gpt_entries);
                return rc;
            }
        }
        pr = usb_media_partition_scan_gpt(gpt_header,
                                          gpt_entries,
                                          USB_MEDIA_GPT_ENTRY_READ_BYTES,
                                          layout);
        *out_gpt = true;
    } else {
        *out_gpt = false;
    }

    free(sector0);
    free(gpt_header);
    free(gpt_entries);
    return (pr == USB_MEDIA_PARTITION_OK) ? ESP_OK : ESP_ERR_NOT_FOUND;
}

static bool rekordbox_export_exists(const char *base_path)
{
    char path[128];
    snprintf(path, sizeof(path), "%s/PIONEER/rekordbox/export.pdb", base_path);
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return false;
    }
    fclose(fp);
    return true;
}

static esp_err_t mount_candidate(msc_host_device_handle_t device,
                                 const char *base_path,
                                 const esp_vfs_fat_mount_config_t *mount_config,
                                 const usb_media_candidate_t *candidate,
                                 bool gpt,
                                 uint32_t sector_size,
                                 usb_media_mount_t **out_mount)
{
    BYTE pdrv;
    esp_err_t rc = ff_diskio_get_drive(&pdrv);
    if (rc != ESP_OK) {
        return rc;
    }
    if (pdrv >= USB_MEDIA_MAX_MOUNTS) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    usb_media_mount_t *mount = calloc(1, sizeof(*mount));
    if (!mount) {
        return ESP_ERR_NO_MEM;
    }
    mount->base_path = strdup(base_path);
    if (!mount->base_path) {
        free(mount);
        return ESP_ERR_NO_MEM;
    }
    mount->pdrv = pdrv;
    mount->device = device;
    mount->base_lba = candidate->first_lba;
    mount->sector_count = candidate->sector_count;
    mount->sector_size = sector_size;
    mount->exfat = candidate->kind == USB_MEDIA_VOLUME_EXFAT;
    mount->gpt = gpt;
    mount->drive[0] = (char)('0' + pdrv);
    mount->drive[1] = ':';
    mount->drive[2] = '\0';

    s_mounts[pdrv] = mount;
    ff_diskio_register(pdrv, &s_translated_diskio);

    esp_vfs_fat_conf_t conf = {
        .base_path = base_path,
        .fat_drive = mount->drive,
        .max_files = mount_config->max_files,
    };
    rc = esp_vfs_fat_register_cfg(&conf, &mount->fs);
    if (rc != ESP_OK) {
        ff_diskio_unregister(pdrv);
        s_mounts[pdrv] = NULL;
        free(mount->base_path);
        free(mount);
        return rc;
    }

    FRESULT fr = f_mount(mount->fs, mount->drive, 1);
    if (fr != FR_OK) {
        ESP_LOGW(TAG, "f_mount failed for LBA %u: FRESULT=%d", (unsigned)candidate->first_lba, (int)fr);
        f_mount(NULL, mount->drive, 0);
        esp_vfs_fat_unregister_path(base_path);
        ff_diskio_unregister(pdrv);
        s_mounts[pdrv] = NULL;
        free(mount->base_path);
        free(mount);
        return ESP_ERR_MSC_MOUNT_FAILED;
    }

    *out_mount = mount;
    return ESP_OK;
}

esp_err_t usb_media_mount(msc_host_device_handle_t device,
                          const char *base_path,
                          const esp_vfs_fat_mount_config_t *mount_config,
                          usb_media_mount_t **out_mount)
{
    if (!device || !base_path || !mount_config || !out_mount) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_mount = NULL;

    msc_host_device_info_t info;
    esp_err_t rc = msc_host_get_device_info(device, &info);
    if (rc != ESP_OK) {
        return rc;
    }

    usb_media_layout_t layout;
    bool gpt = false;
    rc = discover_layout(device, info.sector_size, &layout, &gpt);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "no supported FAT/exFAT USB media layout found: %s", esp_err_to_name(rc));
        return rc;
    }

    usb_media_mount_t *fallback = NULL;
    for (size_t i = 0; i < layout.count; i++) {
        usb_media_mount_t *candidate_mount = NULL;
        rc = mount_candidate(device,
                             base_path,
                             mount_config,
                             &layout.candidates[i],
                             gpt,
                             info.sector_size,
                             &candidate_mount);
        if (rc != ESP_OK) {
            continue;
        }
        if (rekordbox_export_exists(base_path)) {
            *out_mount = candidate_mount;
            ESP_LOGI(TAG, "mounted rekordbox volume at LBA %u%s",
                     (unsigned)candidate_mount->base_lba,
                     candidate_mount->gpt ? " (GPT)" : "");
            return ESP_OK;
        }
        if (!fallback) {
            fallback = candidate_mount;
            continue;
        }
        usb_media_unmount(candidate_mount);
    }

    if (fallback) {
        *out_mount = fallback;
        ESP_LOGW(TAG, "mounted FAT/exFAT volume without rekordbox export at LBA %u",
                 (unsigned)fallback->base_lba);
        return ESP_OK;
    }
    return ESP_ERR_MSC_MOUNT_FAILED;
}

esp_err_t usb_media_unmount(usb_media_mount_t *mount)
{
    if (!mount) {
        return ESP_ERR_INVALID_ARG;
    }
    f_mount(NULL, mount->drive, 0);
    esp_vfs_fat_unregister_path(mount->base_path);
    ff_diskio_unregister(mount->pdrv);
    if (mount->pdrv < USB_MEDIA_MAX_MOUNTS) {
        s_mounts[mount->pdrv] = NULL;
    }
    free(mount->base_path);
    free(mount);
    return ESP_OK;
}

bool usb_media_mount_get_info(const usb_media_mount_t *mount,
                              usb_media_mount_info_t *out_info)
{
    if (!mount || !out_info) {
        return false;
    }
    *out_info = (usb_media_mount_info_t) {
        .base_lba = mount->base_lba,
        .sector_count = mount->sector_count,
        .sector_size = mount->sector_size,
        .exfat = mount->exfat,
        .gpt = mount->gpt,
    };
    return true;
}
```

- [ ] **Step 3: Update the USB storage component build**

Modify `firmware/main-deck-p4/components/usb_storage/CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS "usb_storage.c" "usb_media_partition.c" "usb_media_mount.c"
    INCLUDE_DIRS "include"
    REQUIRES log usb usb_host_msc fatfs media_io_gate
    PRIV_REQUIRES vfs
)
```

- [ ] **Step 4: Build P4 firmware and verify compile failures are limited to exFAT/FatFs config if present**

Run:

```powershell
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1
cd D:\Documents\DDJ-FFL4\firmware\main-deck-p4
idf.py build
```

Expected before integration: build may fail because `usb_media_mount.c` is compiled but not yet referenced by `usb_storage.c`, or because FatFs exFAT override is not present. Resolve only syntax/include errors in this task. Do not integrate runtime behavior until Task 5.

- [ ] **Step 5: Run P4 host tests**

Run:

```powershell
cd D:\Documents\DDJ-FFL4
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
.\tests\run_p4_host_tests.ps1
```

Expected: parser tests and existing host tests pass.

- [ ] **Step 6: Commit mount API and diskio**

```powershell
git add firmware/main-deck-p4/components/usb_storage/include/usb_media_mount.h firmware/main-deck-p4/components/usb_storage/usb_media_mount.c firmware/main-deck-p4/components/usb_storage/CMakeLists.txt
git commit -m "feat: add translated usb media mount layer"
```

## Task 4: P4 FatFs exFAT Override

**Files:**
- Create: `firmware/main-deck-p4/components/fatfs/`
- Modify: `firmware/main-deck-p4/components/fatfs/src/ffconf.h`
- Create: `firmware/main-deck-p4/components/fatfs/README-DDJ-FFL4.md`

- [ ] **Step 1: Copy the ESP-IDF v5.5 FatFs component into the P4 project**

Run from repo root:

```powershell
New-Item -ItemType Directory -Force firmware\main-deck-p4\components\fatfs | Out-Null
Copy-Item -Recurse -Force C:\Espressif\frameworks\esp-idf-v5.5\components\fatfs\* firmware\main-deck-p4\components\fatfs\
```

Expected: `firmware/main-deck-p4/components/fatfs/CMakeLists.txt`, `src/ff.c`, `src/ffconf.h`, `diskio/`, `vfs/`, and `port/` exist in the repo.

- [ ] **Step 2: Enable exFAT in the copied `ffconf.h`**

In `firmware/main-deck-p4/components/fatfs/src/ffconf.h`, replace:

```c
#define FF_FS_EXFAT		0
```

with:

```c
#define FF_FS_EXFAT		1
```

Leave `FF_LBA64` as `0` for this closure.

- [ ] **Step 3: Document the override**

Create `firmware/main-deck-p4/components/fatfs/README-DDJ-FFL4.md`:

```markdown
# DDJ-FFL4 FatFs Override

This project-local `fatfs` component is copied from ESP-IDF v5.5:

`C:\Espressif\frameworks\esp-idf-v5.5\components\fatfs`

Reason for override:

- ESP-IDF v5.5 hardcodes `FF_FS_EXFAT` to `0` in `src/ffconf.h`.
- DDJ-FFL4 must mount rekordbox USB media formatted as exFAT.

Local delta:

- `src/ffconf.h`: `FF_FS_EXFAT` is set to `1`.

Intentional limitation:

- `FF_LBA64` remains `0`. The P4 USB media mount layer handles MBR/GPT partition
  offsets by translating logical sectors to a 32-bit physical base LBA. This
  supports normal DJ USB sticks but is not a general-purpose >2 TiB disk stack.
```

- [ ] **Step 4: Add a compile-time exFAT guard in `usb_media_mount.c`**

At the top of `firmware/main-deck-p4/components/usb_storage/usb_media_mount.c`, after includes:

```c
#if !FF_FS_EXFAT
#error "DDJ-FFL4 P4 USB media mount requires FatFs FF_FS_EXFAT=1"
#endif
```

- [ ] **Step 5: Build P4 firmware and verify the local component override is active**

Run:

```powershell
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1
cd D:\Documents\DDJ-FFL4\firmware\main-deck-p4
idf.py build
```

Expected:

- Build does not hit the `#error`.
- CMake component path output lists `D:/Documents/DDJ-FFL4/firmware/main-deck-p4/components/fatfs` for the `fatfs` component.
- `main-deck-p4.bin` is generated.

- [ ] **Step 6: Commit FatFs override**

```powershell
git add firmware/main-deck-p4/components/fatfs firmware/main-deck-p4/components/usb_storage/usb_media_mount.c
git commit -m "build: enable p4 fatfs exfat support"
```

## Task 5: Integrate New Mount Layer In `usb_storage`

**Files:**
- Modify: `firmware/main-deck-p4/components/usb_storage/usb_storage.c`

- [ ] **Step 1: Replace the VFS handle type**

In `firmware/main-deck-p4/components/usb_storage/usb_storage.c`, replace:

```c
#include "usb/msc_host_vfs.h"
```

with:

```c
#include "usb_media_mount.h"
```

Replace:

```c
static msc_host_vfs_handle_t    s_vfs        = NULL;
```

with:

```c
static usb_media_mount_t       *s_mount      = NULL;
```

- [ ] **Step 2: Replace mount registration**

Replace the current direct mount block:

```c
rc = msc_host_vfs_register(s_msc_dev, USB_STORAGE_MOUNT_POINT, &mount_cfg, &s_vfs);
if (rc != ESP_OK) {
    ESP_LOGE(TAG, "msc_host_vfs_register: %s", esp_err_to_name(rc));
    ESP_LOGE(TAG, "USB mount failed; supported media is FAT32 with an MBR partition table. "
                  "exFAT and FAT32-on-GPT are not supported by the current firmware.");
    msc_host_uninstall_device(s_msc_dev);
    s_msc_dev = NULL;
    continue;
}
```

with:

```c
rc = usb_media_mount(s_msc_dev, USB_STORAGE_MOUNT_POINT, &mount_cfg, &s_mount);
if (rc != ESP_OK) {
    ESP_LOGE(TAG, "usb_media_mount: %s", esp_err_to_name(rc));
    ESP_LOGE(TAG, "USB mount failed; supported media is FAT32/exFAT on superfloppy, MBR, or GPT layout.");
    msc_host_uninstall_device(s_msc_dev);
    s_msc_dev = NULL;
    continue;
}

usb_media_mount_info_t mount_info;
if (usb_media_mount_get_info(s_mount, &mount_info)) {
    ESP_LOGI(TAG, "USB media mounted: base_lba=%u sectors=%u sector_size=%u exfat=%u gpt=%u",
             (unsigned)mount_info.base_lba,
             (unsigned)mount_info.sector_count,
             (unsigned)mount_info.sector_size,
             mount_info.exfat ? 1u : 0u,
             mount_info.gpt ? 1u : 0u);
}
```

- [ ] **Step 3: Replace unmount cleanup**

Replace:

```c
if (s_vfs) {
    msc_host_vfs_unregister(s_vfs);
    s_vfs = NULL;
}
```

with:

```c
if (s_mount) {
    usb_media_unmount(s_mount);
    s_mount = NULL;
}
```

- [ ] **Step 4: Run P4 firmware build**

Run:

```powershell
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1
cd D:\Documents\DDJ-FFL4\firmware\main-deck-p4
idf.py build
```

Expected: build passes and generated binary size still fits the app partition.

- [ ] **Step 5: Run P4 host tests**

Run:

```powershell
cd D:\Documents\DDJ-FFL4
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
.\tests\run_p4_host_tests.ps1
```

Expected: `P4 host tests passed.`

- [ ] **Step 6: Commit integration**

```powershell
git add firmware/main-deck-p4/components/usb_storage/usb_storage.c
git commit -m "feat: mount usb media through exfat gpt adapter"
```

## Task 6: Candidate Validation With Boot Sector Hints

**Files:**
- Modify: `firmware/main-deck-p4/components/usb_storage/usb_media_mount.c`
- Modify: `tests/usb_media_partition/test_usb_media_partition.c`

- [ ] **Step 1: Add tests for candidate boot-sector refinement**

Append this test to `tests/usb_media_partition/test_usb_media_partition.c` and call it from `main()` before `puts(...)`:

```c
static void test_mbr_type_07_becomes_exfat_after_boot_sector_classification(void)
{
    uint8_t mbr[512];
    uint8_t boot[512];
    make_mbr(mbr, 0x07u, 8192u, 262144u);
    make_exfat_boot(boot);

    usb_media_layout_t layout;
    assert(usb_media_partition_scan_mbr_or_sfd(mbr, 512u, &layout) == USB_MEDIA_PARTITION_OK);
    assert(layout.count == 1u);
    assert(layout.candidates[0].kind == USB_MEDIA_VOLUME_UNKNOWN);
    layout.candidates[0].kind = usb_media_partition_classify_boot_sector(boot, 512u);
    assert(layout.candidates[0].kind == USB_MEDIA_VOLUME_EXFAT);
}
```

Update `main()`:

```c
    test_mbr_type_07_becomes_exfat_after_boot_sector_classification();
```

- [ ] **Step 2: Run test and verify RED/GREEN boundary**

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
.\tests\run_p4_host_tests.ps1
```

Expected: tests pass because the pure classifier already supports this. This locks the behavior before wiring runtime validation.

- [ ] **Step 3: Refine candidate kind before mount**

In `usb_media_mount.c`, add this helper:

```c
static void refine_candidate_kind(msc_host_device_handle_t device,
                                  uint32_t sector_size,
                                  usb_media_candidate_t *candidate)
{
    if (!device || !candidate) {
        return;
    }
    uint8_t *boot = calloc(1, sector_size);
    if (!boot) {
        return;
    }
    if (read_sector(device, candidate->first_lba, sector_size, boot) == ESP_OK) {
        usb_media_volume_kind_t kind = usb_media_partition_classify_boot_sector(boot, sector_size);
        if (kind != USB_MEDIA_VOLUME_UNKNOWN) {
            candidate->kind = kind;
        }
    }
    free(boot);
}
```

In `usb_media_mount()`, before calling `mount_candidate(...)`, add:

```c
        refine_candidate_kind(device, info.sector_size, &layout.candidates[i]);
```

- [ ] **Step 4: Log exFAT candidate detection**

In `usb_media_mount()`, after refinement, add:

```c
        if (layout.candidates[i].kind == USB_MEDIA_VOLUME_EXFAT) {
            ESP_LOGI(TAG, "exFAT candidate detected at LBA %u", (unsigned)layout.candidates[i].first_lba);
        }
```

- [ ] **Step 5: Run P4 host tests and firmware build**

Run:

```powershell
cd D:\Documents\DDJ-FFL4
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
.\tests\run_p4_host_tests.ps1

$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1
cd D:\Documents\DDJ-FFL4\firmware\main-deck-p4
idf.py build
```

Expected: host suite and IDF build pass.

- [ ] **Step 6: Commit candidate validation**

```powershell
git add firmware/main-deck-p4/components/usb_storage/usb_media_mount.c tests/usb_media_partition/test_usb_media_partition.c
git commit -m "feat: validate usb media candidates by boot sector"
```

## Task 7: Documentation And Hardware Smoke Checklist

**Files:**
- Modify: `docs/bench-notes.md`
- Create: `docs/validation/P4_USB_EXFAT_GPT_SMOKE.md`

- [ ] **Step 1: Create hardware smoke document**

Create `docs/validation/P4_USB_EXFAT_GPT_SMOKE.md`:

```markdown
# P4 USB exFAT + GPT Smoke

Date:
Firmware branch:
P4 serial port:
ESP-IDF version: v5.5

## Test Matrix

| Case | Media layout | Filesystem | Expected | Result | Notes |
|---|---|---|---|---|---|
| 1 | MBR | FAT32 | `/usb` mounts, `export.pdb` loads, one track opens |  | Baseline regression |
| 2 | GPT | FAT32 | `/usb` mounts, `export.pdb` loads, one track opens |  | Regression from bench notes |
| 3 | MBR | exFAT | `/usb` mounts, `export.pdb` loads, one track opens |  | exFAT MBR |
| 4 | GPT | exFAT | `/usb` mounts, `export.pdb` loads, one track opens |  | Primary target |

## Required Log Evidence

Paste representative boot/mount lines showing:

```text
USB MSC device:
USB media mounted:
USB media library loaded:
```

For exFAT cases, include:

```text
exFAT candidate detected
```

## Acceptance

The smoke passes when all four rows mount at `/usb`, the library parser reports a positive track count, and at least one track can be loaded without `NOT FOUND`, `NO FILESYSTEM`, or USB MSC reset errors.
```

- [ ] **Step 2: Update bench notes open item**

In `docs/bench-notes.md`, replace the current open item that says exFAT and GPT remain backlog with:

```markdown
- ⚙️ **USB filesystem/layout support:** P4 firmware now has a planned closure for
  FAT32/exFAT across superfloppy, MBR, and GPT layouts in
  `docs/superpowers/specs/2026-07-03-p4-usb-exfat-gpt-design.md` and
  `docs/superpowers/plans/2026-07-03-p4-usb-exfat-gpt.md`. Hardware acceptance
  is tracked in `docs/validation/P4_USB_EXFAT_GPT_SMOKE.md`.
```

After hardware smoke passes, update this same item to `**FIXED**` and summarize the four tested media rows.

- [ ] **Step 3: Run documentation checks**

Run:

```powershell
git diff --check -- docs/bench-notes.md docs/validation/P4_USB_EXFAT_GPT_SMOKE.md
```

Expected: no output and exit code 0.

- [ ] **Step 4: Commit docs**

```powershell
git add docs/bench-notes.md docs/validation/P4_USB_EXFAT_GPT_SMOKE.md
git commit -m "docs: add p4 usb exfat gpt validation"
```

## Task 8: Final Verification And Cleanup

**Files:**
- No planned source edits unless verification exposes a defect.

- [ ] **Step 1: Run P4 host tests**

Run:

```powershell
cd D:\Documents\DDJ-FFL4
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
.\tests\run_p4_host_tests.ps1
```

Expected: `P4 host tests passed.`

- [ ] **Step 2: Run P4 firmware build**

Run:

```powershell
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1
cd D:\Documents\DDJ-FFL4\firmware\main-deck-p4
idf.py build
```

Expected: `Project build complete.` and binary size fits app partition.

- [ ] **Step 3: Run whitespace check**

Run:

```powershell
cd D:\Documents\DDJ-FFL4
git diff --check
```

Expected: no errors.

- [ ] **Step 4: Confirm no generated artifacts are staged**

Run:

```powershell
git status --short
```

Expected:

- source, tests, docs, and FatFs override files are committed;
- ignored `build/`, `managed_components/`, `sdkconfig`, and test executables are not staged;
- any pre-existing unrelated user changes are still present and not included in these commits.

- [ ] **Step 5: Hardware smoke**

Flash P4 and run the four media cases from `docs/validation/P4_USB_EXFAT_GPT_SMOKE.md`.

Expected for each case:

```text
USB MSC device:
USB media mounted:
USB media library loaded:
```

For exFAT media, expected additional line:

```text
exFAT candidate detected
```

- [ ] **Step 6: Record final hardware results**

Update `docs/validation/P4_USB_EXFAT_GPT_SMOKE.md` with the actual date, branch, serial port, and result rows.

If all four rows pass, update `docs/bench-notes.md` open item from `⚙️` to `✅ **USB filesystem/layout support:**` and mention FAT32 MBR, FAT32 GPT, exFAT MBR, and exFAT GPT.

- [ ] **Step 7: Commit final validation notes**

```powershell
git add docs/bench-notes.md docs/validation/P4_USB_EXFAT_GPT_SMOKE.md
git commit -m "docs: record p4 usb exfat gpt smoke"
```

## Self-Review

Spec coverage:

- exFAT support is covered by Task 4.
- GPT and MBR discovery are covered by Tasks 1, 2, and 6.
- Keeping `/usb` unchanged is covered by Tasks 3 and 5.
- Existing library/audio paths are preserved by Task 5.
- 32-bit sector limitation is documented in the plan header and FatFs override README in Task 4.
- Error logging and cleanup are covered by Tasks 3 and 5.
- Host tests, firmware build, and hardware smoke are covered by Tasks 1, 2, 6, 7, and 8.

No red-flag terms are intentionally left in this plan. Function names introduced in tests match the headers and implementation tasks that define them.
