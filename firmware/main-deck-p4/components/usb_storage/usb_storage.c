#include "usb_storage.h"
#include "media_io_gate.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "usb/usb_host.h"
#include "usb/msc_host.h"
#include "usb_media_mount.h"
#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <string.h>

static const char *TAG = "usb_storage";

#define USB_LIB_TASK_STACK   4096
#define USB_LIB_TASK_PRIO    4
#define MSC_TASK_STACK       4096
#define MSC_TASK_PRIO        5
// Generous: the mount callback runs library_init() (PDB parse) + ui_trigger_library_refresh()
// on this task.
#define STORAGE_TASK_STACK   (16 * 1024)
#define STORAGE_TASK_PRIO    3

typedef struct {
    enum { EVT_CONNECTED, EVT_DISCONNECTED } id;
    uint8_t dev_addr;
} storage_msg_t;

static QueueHandle_t          s_queue       = NULL;
static TaskHandle_t           s_storage_task = NULL;
static TaskHandle_t           s_usb_lib_task = NULL;
static usb_storage_event_cb_t s_cb          = NULL;
static volatile bool          s_mounted     = false;
static msc_host_device_handle_t s_msc_dev   = NULL;
static usb_media_mount_t       *s_mount      = NULL;
static uint32_t              s_event_drop_count;
static TickType_t            s_last_drop_warn;

bool usb_storage_is_mounted(void)
{
    return s_mounted;
}

// ── MSC connect/disconnect callback (driver task context) ────────────────────
static void msc_event_cb(const msc_host_event_t *event, void *arg)
{
    (void)arg;
    storage_msg_t msg;
    if (event->event == MSC_DEVICE_CONNECTED) {
        msg.id = EVT_CONNECTED;
        msg.dev_addr = event->device.address;
        if (xQueueSend(s_queue, &msg, 0) != pdTRUE) {
            s_event_drop_count++;
        }
    } else if (event->event == MSC_DEVICE_DISCONNECTED) {
        msg.id = EVT_DISCONNECTED;
        msg.dev_addr = 0;
        if (xQueueSend(s_queue, &msg, 0) != pdTRUE) {
            s_event_drop_count++;
        }
    }

    if (s_event_drop_count > 0) {
        TickType_t now = xTaskGetTickCount();
        if (now - s_last_drop_warn >= pdMS_TO_TICKS(1000)) {
            s_last_drop_warn = now;
            ESP_LOGW(TAG, "USB storage event queue drops=%" PRIu32, s_event_drop_count);
        }
    }
}

// ── USB Host library event task ──────────────────────────────────────────────
static void usb_lib_task(void *arg)
{
    (void)arg;
    const usb_host_config_t host_cfg = { .intr_flags = ESP_INTR_FLAG_LEVEL1 };
    ESP_ERROR_CHECK(usb_host_install(&host_cfg));

    const msc_host_driver_config_t msc_cfg = {
        .create_backround_task = true,
        .task_priority         = MSC_TASK_PRIO,
        .stack_size            = MSC_TASK_STACK,
        .callback              = msc_event_cb,
    };
    ESP_ERROR_CHECK(msc_host_install(&msc_cfg));

    ESP_LOGI(TAG, "USB host + MSC installed; waiting for a drive on the HS USB port");

    while (1) {
        uint32_t flags;
        usb_host_lib_handle_events(portMAX_DELAY, &flags);
        if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            usb_host_device_free_all();
        }
        // Note: we never uninstall — the host runs for the lifetime of the device.
    }
}

// ── Storage handler task: mount/unmount FATFS at /usb ────────────────────────
static void storage_task(void *arg)
{
    (void)arg;
    while (1) {
        storage_msg_t msg;
        if (xQueueReceive(s_queue, &msg, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (msg.id == EVT_CONNECTED) {
            if (s_mounted) {
                ESP_LOGW(TAG, "a drive is already mounted; ignoring second device");
                continue;
            }
            ESP_LOGI(TAG, "drive connected (addr=%d), mounting at %s", msg.dev_addr, USB_STORAGE_MOUNT_POINT);
            esp_err_t rc = msc_host_install_device(msg.dev_addr, &s_msc_dev);
            if (rc != ESP_OK) {
                ESP_LOGE(TAG, "msc_host_install_device: %s", esp_err_to_name(rc));
                continue;
            }
            msc_host_device_info_t info;
            if (msc_host_get_device_info(s_msc_dev, &info) == ESP_OK) {
                uint64_t mb = ((uint64_t)info.sector_size * info.sector_count) / (1024 * 1024);
                ESP_LOGI(TAG, "USB MSC device: %llu MB, sector=%u bytes (VID:0x%04X PID:0x%04X)",
                         mb,
                         (unsigned)info.sector_size,
                         info.idVendor,
                         info.idProduct);
            }
            const esp_vfs_fat_mount_config_t mount_cfg = {
                .format_if_mount_failed = false,
                .max_files              = 5,
                .allocation_unit_size   = 8192,
            };
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
            if (msc_host_get_device_info(s_msc_dev, &info) == ESP_OK) {
                uint64_t mb = ((uint64_t)info.sector_size * info.sector_count) / (1024 * 1024);
                ESP_LOGI(TAG, "mounted: %llu MB (VID:0x%04X PID:0x%04X)", mb, info.idVendor, info.idProduct);
            }

            // Debug: list the drive root so we can confirm long filenames resolve
            // (PIONEER/, Contents/, etc.) before the library parser runs.
            DIR *dh = opendir(USB_STORAGE_MOUNT_POINT);
            if (dh) {
                struct dirent *d;
                int n = 0;
                while ((d = readdir(dh)) != NULL && n < 24) {
                    ESP_LOGI(TAG, "  /usb/%s", d->d_name);
                    n++;
                }
                closedir(dh);
            } else {
                ESP_LOGW(TAG, "opendir(%s) failed: %s", USB_STORAGE_MOUNT_POINT, strerror(errno));
            }

            s_mounted = true;
            if (s_cb) {
                s_cb(true);
            }
        } else { // EVT_DISCONNECTED
            ESP_LOGW(TAG, "drive disconnected");
            s_mounted = false;
            if (s_cb) {
                s_cb(false);
            }
            media_io_gate_begin();
            if (s_mount) {
                usb_media_unmount(s_mount);
                s_mount = NULL;
            }
            if (s_msc_dev) {
                msc_host_uninstall_device(s_msc_dev);
                s_msc_dev = NULL;
            }
            media_io_gate_end();
        }
    }
}

esp_err_t usb_storage_init(usb_storage_event_cb_t cb)
{
    s_cb = cb;
    s_queue = xQueueCreate(4, sizeof(storage_msg_t));
    if (!s_queue) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(storage_task, "usb_store", STORAGE_TASK_STACK, NULL, STORAGE_TASK_PRIO, &s_storage_task) != pdPASS) {
        vQueueDelete(s_queue);
        s_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(usb_lib_task, "usb_lib", USB_LIB_TASK_STACK, NULL, USB_LIB_TASK_PRIO, &s_usb_lib_task) != pdPASS) {
        vTaskDelete(s_storage_task);
        s_storage_task = NULL;
        vQueueDelete(s_queue);
        s_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
