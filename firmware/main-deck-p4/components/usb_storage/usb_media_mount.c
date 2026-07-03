#include "usb_media_mount.h"

#include "diskio_impl.h"
#include "esp_log.h"
#include "esp_private/msc_scsi_bot.h"
#include "ff.h"
#include "usb/msc_host.h"
#include "usb_media_partition.h"

#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !FF_FS_EXFAT
#error "DDJ-FFL4 P4 USB media mount requires FatFs FF_FS_EXFAT=1"
#endif

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

static uint32_t read_u32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint64_t read_u64le(const uint8_t *p)
{
    return (uint64_t)read_u32le(p) | ((uint64_t)read_u32le(p + 4) << 32);
}

static DSTATUS translated_initialize(BYTE pdrv)
{
    return (pdrv < USB_MEDIA_MAX_MOUNTS && s_mounts[pdrv] != NULL) ? 0 : STA_NOINIT;
}

static DSTATUS translated_status(BYTE pdrv)
{
    return (pdrv < USB_MEDIA_MAX_MOUNTS && s_mounts[pdrv] != NULL) ? 0 : STA_NOINIT;
}

static bool logical_range_is_valid(const usb_media_mount_t *mount,
                                   uint32_t sector,
                                   uint32_t count,
                                   uint32_t *out_physical_lba)
{
    if (mount == NULL || count == 0u || sector >= mount->sector_count ||
        count > (mount->sector_count - sector)) {
        return false;
    }

    if (sector > (UINT32_MAX - mount->base_lba)) {
        return false;
    }

    const uint32_t physical_lba = mount->base_lba + sector;
    if ((count - 1u) > (UINT32_MAX - physical_lba)) {
        return false;
    }

    *out_physical_lba = physical_lba;
    return true;
}

static DRESULT translated_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count)
{
    if (pdrv >= USB_MEDIA_MAX_MOUNTS || s_mounts[pdrv] == NULL || buff == NULL) {
        return RES_PARERR;
    }

    uint32_t physical_lba;
    usb_media_mount_t *mount = s_mounts[pdrv];
    if (!logical_range_is_valid(mount, sector, count, &physical_lba)) {
        return RES_PARERR;
    }

    esp_err_t rc = scsi_cmd_read10(mount->device,
                                   buff,
                                   physical_lba,
                                   count,
                                   mount->sector_size);
    return rc == ESP_OK ? RES_OK : RES_ERROR;
}

static DRESULT translated_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count)
{
    if (pdrv >= USB_MEDIA_MAX_MOUNTS || s_mounts[pdrv] == NULL || buff == NULL) {
        return RES_PARERR;
    }

    uint32_t physical_lba;
    usb_media_mount_t *mount = s_mounts[pdrv];
    if (!logical_range_is_valid(mount, sector, count, &physical_lba)) {
        return RES_PARERR;
    }

    esp_err_t rc = scsi_cmd_write10(mount->device,
                                    buff,
                                    physical_lba,
                                    count,
                                    mount->sector_size);
    return rc == ESP_OK ? RES_OK : RES_ERROR;
}

static DRESULT translated_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if (pdrv >= USB_MEDIA_MAX_MOUNTS || s_mounts[pdrv] == NULL) {
        return RES_PARERR;
    }

    usb_media_mount_t *mount = s_mounts[pdrv];
    switch (cmd) {
    case CTRL_SYNC:
        return RES_OK;
    case GET_SECTOR_COUNT:
        if (buff == NULL) {
            return RES_PARERR;
        }
        *(DWORD *)buff = mount->sector_count;
        return RES_OK;
    case GET_SECTOR_SIZE:
        if (buff == NULL || mount->sector_size > UINT16_MAX) {
            return RES_PARERR;
        }
        *(WORD *)buff = (WORD)mount->sector_size;
        return RES_OK;
    case GET_BLOCK_SIZE:
        if (buff == NULL) {
            return RES_PARERR;
        }
        *(DWORD *)buff = 1u;
        return RES_OK;
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

static esp_err_t read_sectors(msc_host_device_handle_t device,
                              uint32_t start_lba,
                              uint32_t count,
                              uint32_t sector_size,
                              void *buf)
{
    if (device == NULL || buf == NULL || count == 0u ||
        (count - 1u) > (UINT32_MAX - start_lba)) {
        return ESP_ERR_INVALID_ARG;
    }

    return scsi_cmd_read10(device, buf, start_lba, count, sector_size);
}

static esp_err_t discover_layout(msc_host_device_handle_t device,
                                 uint32_t sector_size,
                                 usb_media_layout_t *layout,
                                 bool *out_gpt)
{
    if (device == NULL || layout == NULL || out_gpt == NULL ||
        sector_size < USB_MEDIA_SECTOR_SIZE_MIN) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t *sector0 = calloc(1, sector_size);
    uint8_t *gpt_header = calloc(1, sector_size);
    uint8_t *gpt_entries = calloc(1, USB_MEDIA_GPT_ENTRY_READ_BYTES);
    if (sector0 == NULL || gpt_header == NULL || gpt_entries == NULL) {
        free(sector0);
        free(gpt_header);
        free(gpt_entries);
        return ESP_ERR_NO_MEM;
    }

    esp_err_t rc = read_sectors(device, 0u, 1u, sector_size, sector0);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "read sector 0 failed: %s", esp_err_to_name(rc));
        goto out;
    }

    usb_media_partition_result_t pr =
        usb_media_partition_scan_mbr_or_sfd(sector0, sector_size, layout);
    if (pr == USB_MEDIA_PARTITION_NEEDS_GPT) {
        rc = read_sectors(device, 1u, 1u, sector_size, gpt_header);
        if (rc != ESP_OK) {
            ESP_LOGE(TAG, "read GPT header failed: %s", esp_err_to_name(rc));
            goto out;
        }

        const uint64_t entries_lba = read_u64le(gpt_header + 72);
        const uint32_t entry_size = read_u32le(gpt_header + 84);
        if (entries_lba > UINT32_MAX || entry_size < 128u || entry_size > 512u ||
            (USB_MEDIA_GPT_ENTRY_READ_BYTES % sector_size) != 0u) {
            rc = ESP_ERR_INVALID_SIZE;
            goto out;
        }

        const uint32_t sectors_to_read = USB_MEDIA_GPT_ENTRY_READ_BYTES / sector_size;
        if (sectors_to_read == 0u ||
            (sectors_to_read - 1u) > (UINT32_MAX - (uint32_t)entries_lba)) {
            rc = ESP_ERR_INVALID_SIZE;
            goto out;
        }

        rc = read_sectors(device,
                          (uint32_t)entries_lba,
                          sectors_to_read,
                          sector_size,
                          gpt_entries);
        if (rc != ESP_OK) {
            ESP_LOGE(TAG, "read GPT entries failed: %s", esp_err_to_name(rc));
            goto out;
        }

        pr = usb_media_partition_scan_gpt(gpt_header,
                                          gpt_entries,
                                          USB_MEDIA_GPT_ENTRY_READ_BYTES,
                                          layout);
        *out_gpt = true;
    } else {
        *out_gpt = false;
    }

    rc = (pr == USB_MEDIA_PARTITION_OK) ? ESP_OK : ESP_ERR_NOT_FOUND;

out:
    free(sector0);
    free(gpt_header);
    free(gpt_entries);
    return rc;
}

static bool rekordbox_export_exists(const char *base_path)
{
    char path[160];
    int n = snprintf(path, sizeof(path), "%s/PIONEER/rekordbox/export.pdb", base_path);
    if (n < 0 || (size_t)n >= sizeof(path)) {
        return false;
    }

    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        return false;
    }

    fclose(fp);
    return true;
}

static esp_err_t copy_base_path(usb_media_mount_t *mount, const char *base_path)
{
    const size_t len = strlen(base_path) + 1u;
    mount->base_path = malloc(len);
    if (mount->base_path == NULL) {
        return ESP_ERR_NO_MEM;
    }
    memcpy(mount->base_path, base_path, len);
    return ESP_OK;
}

static bool candidate_effective_sector_count(const usb_media_candidate_t *candidate,
                                             uint32_t device_sector_count,
                                             uint32_t *out_sector_count)
{
    if (candidate == NULL || out_sector_count == NULL ||
        candidate->first_lba >= device_sector_count) {
        return false;
    }

    const uint32_t available = device_sector_count - candidate->first_lba;
    if (candidate->first_lba == 0u && candidate->sector_count == UINT32_MAX) {
        *out_sector_count = available;
        return available > 0u;
    }

    if (candidate->sector_count == 0u || candidate->sector_count > available) {
        return false;
    }

    *out_sector_count = candidate->sector_count;
    return true;
}

static void refine_candidate_kind_from_boot_sector(msc_host_device_handle_t device,
                                                   uint32_t sector_size,
                                                   uint32_t device_sector_count,
                                                   usb_media_candidate_t *candidate)
{
    if (device == NULL || candidate == NULL || sector_size < USB_MEDIA_SECTOR_SIZE_MIN) {
        return;
    }

    uint32_t ignored_sector_count;
    if (!candidate_effective_sector_count(candidate, device_sector_count, &ignored_sector_count)) {
        return;
    }

    uint8_t *boot = calloc(1, sector_size);
    if (boot == NULL) {
        return;
    }

    const esp_err_t rc = read_sectors(device, candidate->first_lba, 1u, sector_size, boot);
    if (rc == ESP_OK) {
        const usb_media_volume_kind_t kind =
            usb_media_partition_classify_boot_sector(boot, sector_size);
        if (kind == USB_MEDIA_VOLUME_FAT || kind == USB_MEDIA_VOLUME_EXFAT) {
            candidate->kind = kind;
        }
    }

    free(boot);
}

static void free_mount_object(usb_media_mount_t *mount)
{
    if (mount == NULL) {
        return;
    }
    free(mount->base_path);
    free(mount);
}

static esp_err_t mount_candidate(msc_host_device_handle_t device,
                                 const char *base_path,
                                 const esp_vfs_fat_mount_config_t *mount_config,
                                 const usb_media_candidate_t *candidate,
                                 bool gpt,
                                 uint32_t sector_size,
                                 uint32_t device_sector_count,
                                 usb_media_mount_t **out_mount)
{
    if (out_mount == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_mount = NULL;

    uint32_t effective_sector_count;
    if (!candidate_effective_sector_count(candidate, device_sector_count, &effective_sector_count)) {
        return ESP_ERR_INVALID_SIZE;
    }

    BYTE pdrv;
    esp_err_t rc = ff_diskio_get_drive(&pdrv);
    if (rc != ESP_OK) {
        return rc;
    }
    if (pdrv >= USB_MEDIA_MAX_MOUNTS) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    usb_media_mount_t *mount = calloc(1, sizeof(*mount));
    if (mount == NULL) {
        return ESP_ERR_NO_MEM;
    }

    rc = copy_base_path(mount, base_path);
    if (rc != ESP_OK) {
        free_mount_object(mount);
        return rc;
    }

    mount->pdrv = pdrv;
    mount->device = device;
    mount->base_lba = candidate->first_lba;
    mount->sector_count = effective_sector_count;
    mount->sector_size = sector_size;
    mount->exfat = candidate->kind == USB_MEDIA_VOLUME_EXFAT;
    mount->gpt = gpt;
    mount->drive[0] = (char)('0' + pdrv);
    mount->drive[1] = ':';
    mount->drive[2] = '\0';

    s_mounts[pdrv] = mount;
    ff_diskio_register(pdrv, &s_translated_diskio);

    const esp_vfs_fat_conf_t conf = {
        .base_path = base_path,
        .fat_drive = mount->drive,
        .max_files = mount_config->max_files,
    };
    rc = esp_vfs_fat_register_cfg(&conf, &mount->fs);
    if (rc != ESP_OK) {
        ff_diskio_unregister(pdrv);
        s_mounts[pdrv] = NULL;
        free_mount_object(mount);
        return rc;
    }

    FRESULT fr = f_mount(mount->fs, mount->drive, 1);
    if (fr != FR_OK) {
        ESP_LOGW(TAG, "f_mount failed for LBA %u: FRESULT=%d",
                 (unsigned)candidate->first_lba,
                 (int)fr);
        (void)f_mount(NULL, mount->drive, 0);
        (void)esp_vfs_fat_unregister_path(base_path);
        ff_diskio_unregister(pdrv);
        s_mounts[pdrv] = NULL;
        free_mount_object(mount);
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
    if (device == NULL || base_path == NULL || mount_config == NULL || out_mount == NULL) {
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

    usb_media_candidate_t fallback_candidate = {0};
    bool have_fallback = false;

    for (size_t i = 0; i < layout.count; ++i) {
        usb_media_mount_t *candidate_mount = NULL;
        refine_candidate_kind_from_boot_sector(device,
                                               info.sector_size,
                                               info.sector_count,
                                               &layout.candidates[i]);
        if (layout.candidates[i].kind == USB_MEDIA_VOLUME_EXFAT) {
            ESP_LOGI(TAG, "exFAT candidate detected at LBA %u",
                     (unsigned)layout.candidates[i].first_lba);
        }
        rc = mount_candidate(device,
                             base_path,
                             mount_config,
                             &layout.candidates[i],
                             gpt,
                             info.sector_size,
                             info.sector_count,
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

        if (!have_fallback) {
            fallback_candidate = layout.candidates[i];
            have_fallback = true;
        }
        (void)usb_media_unmount(candidate_mount);
    }

    if (have_fallback) {
        usb_media_mount_t *fallback_mount = NULL;
        rc = mount_candidate(device,
                             base_path,
                             mount_config,
                             &fallback_candidate,
                             gpt,
                             info.sector_size,
                             info.sector_count,
                             &fallback_mount);
        if (rc == ESP_OK) {
            *out_mount = fallback_mount;
            ESP_LOGW(TAG, "mounted FAT/exFAT volume without rekordbox export at LBA %u",
                     (unsigned)fallback_mount->base_lba);
        }
        return rc;
    }

    return ESP_ERR_MSC_MOUNT_FAILED;
}

esp_err_t usb_media_unmount(usb_media_mount_t *mount)
{
    if (mount == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t rc = ESP_OK;
    FRESULT fr = f_mount(NULL, mount->drive, 0);
    if (fr != FR_OK) {
        rc = ESP_FAIL;
    }

    esp_err_t unregister_rc = esp_vfs_fat_unregister_path(mount->base_path);
    if (unregister_rc != ESP_OK && rc == ESP_OK) {
        rc = unregister_rc;
    }

    ff_diskio_unregister(mount->pdrv);
    if (mount->pdrv < USB_MEDIA_MAX_MOUNTS) {
        s_mounts[mount->pdrv] = NULL;
    }
    free_mount_object(mount);
    return rc;
}

bool usb_media_mount_get_info(const usb_media_mount_t *mount,
                              usb_media_mount_info_t *out_info)
{
    if (mount == NULL || out_info == NULL) {
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
