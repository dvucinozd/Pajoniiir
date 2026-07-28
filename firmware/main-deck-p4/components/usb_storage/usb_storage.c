#include "usb_storage.h"
#include "media_io_gate.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "usb/usb_host.h"
#include "usb/msc_host.h"
#include "usb_media_mount.h"

#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>

static const char *TAG = "usb_storage";

#define USB_LIB_TASK_STACK       4096
#define USB_LIB_TASK_PRIO        4
#define MSC_TASK_STACK           4096
#define MSC_TASK_PRIO            5
/* The mount callback runs library_init() (PDB parse) + UI refresh. */
#define STORAGE_TASK_STACK       (16 * 1024)
#define STORAGE_TASK_PRIO        3

#define CONNECT_STABLE_MS        350u
#define RECONCILE_POLL_MS        500u
#define MOUNT_RETRY_INITIAL_MS   500u
#define MOUNT_RETRY_MAX_MS       30000u

/* Root-port recovery for a drive that remained powered across a software reset. */
#define ROOT_PORT_SETTLE_MS      150u
#define ROOT_PORT_RETRY_MS       900u
#define ROOT_PORT_MAX_CYCLES     8u
#define ROOT_PORT_SLOW_MS        30000u

typedef struct {
    bool connected;
    uint8_t dev_addr;
    uint32_t epoch;
} storage_desired_t;

static TaskHandle_t             s_storage_task;
static TaskHandle_t             s_usb_lib_task;
static usb_storage_event_cb_t   s_cb;
static msc_host_device_handle_t s_msc_dev;
static usb_media_mount_t       *s_mount;

/* Driver callback publishes only desired state. The storage task is the sole
 * owner of mount/unmount handles and callback publication. */
static portMUX_TYPE s_state_mux = portMUX_INITIALIZER_UNLOCKED;
static storage_desired_t s_desired;
static bool s_mounted;
static bool s_announced_mounted;
static bool s_seen_device;

static storage_desired_t desired_snapshot(void)
{
    storage_desired_t snapshot;
    portENTER_CRITICAL(&s_state_mux);
    snapshot = s_desired;
    portEXIT_CRITICAL(&s_state_mux);
    return snapshot;
}

static bool desired_matches(uint32_t epoch, uint8_t dev_addr)
{
    bool matches;
    portENTER_CRITICAL(&s_state_mux);
    matches = s_desired.connected &&
              s_desired.epoch == epoch &&
              s_desired.dev_addr == dev_addr;
    portEXIT_CRITICAL(&s_state_mux);
    return matches;
}

static void notify_storage_owner(void)
{
    TaskHandle_t task = s_storage_task;
    if (task) {
        xTaskNotifyGive(task);
    }
}

static void publish_desired_connection(bool connected, uint8_t dev_addr)
{
    bool accepted = true;

    portENTER_CRITICAL(&s_state_mux);
    if (connected) {
        /* This component intentionally owns one Rekordbox drive. Ignore a second
         * address rather than evicting a working device underneath active reads. */
        if (s_desired.connected &&
            s_desired.dev_addr != 0u &&
            s_desired.dev_addr != dev_addr) {
            accepted = false;
        } else {
            s_desired.connected = true;
            s_desired.dev_addr = dev_addr;
            s_seen_device = true;
            s_desired.epoch++;
        }
    } else {
        /* Disconnect is level state, not a lossy edge. Even if notifications
         * coalesce, the owner will observe connected=false on its next poll. */
        s_desired.connected = false;
        s_desired.dev_addr = 0u;
        s_desired.epoch++;
        s_mounted = false;
    }
    portEXIT_CRITICAL(&s_state_mux);

    if (!accepted) {
        ESP_LOGW(TAG, "second USB storage device ignored (addr=%u)",
                 (unsigned)dev_addr);
        return;
    }

    if (!connected) {
        /* Block new media reads immediately. The owner task performs the safe
         * unmount after existing gate holders have drained. */
        media_io_gate_set_available(false);
    }
    notify_storage_owner();
}

static void root_port_power_cycle(const char *why)
{
    ESP_LOGI(TAG, "root port power cycle (%s)", why ? why : "");
    esp_err_t rc = usb_host_lib_set_root_port_power(false);
    if (rc != ESP_OK && rc != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "root port power off: %s", esp_err_to_name(rc));
    }
    vTaskDelay(pdMS_TO_TICKS(ROOT_PORT_SETTLE_MS));
    rc = usb_host_lib_set_root_port_power(true);
    if (rc != ESP_OK && rc != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "root port power on: %s", esp_err_to_name(rc));
    }
}

static void release_device(void)
{
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

static void publish_unmounted(void)
{
    bool notify = false;
    portENTER_CRITICAL(&s_state_mux);
    s_mounted = false;
    if (s_announced_mounted) {
        s_announced_mounted = false;
        notify = true;
    }
    portEXIT_CRITICAL(&s_state_mux);

    media_io_gate_set_available(false);
    if (notify && s_cb) {
        s_cb(false);
    }
}

static bool commit_mounted(uint32_t epoch, uint8_t dev_addr)
{
    /* Enable the gate before the final state check. If disconnect races after
     * this point, its callback always executes the later set_available(false). */
    media_io_gate_set_available(true);

    bool committed = false;
    portENTER_CRITICAL(&s_state_mux);
    if (s_desired.connected &&
        s_desired.epoch == epoch &&
        s_desired.dev_addr == dev_addr) {
        s_mounted = true;
        s_announced_mounted = true;
        committed = true;
    }
    portEXIT_CRITICAL(&s_state_mux);

    if (!committed) {
        media_io_gate_set_available(false);
        return false;
    }
    if (s_cb) {
        s_cb(true);
    }
    return true;
}

bool usb_storage_is_mounted(void)
{
    bool mounted;
    portENTER_CRITICAL(&s_state_mux);
    mounted = s_mounted;
    portEXIT_CRITICAL(&s_state_mux);
    return mounted;
}

/* MSC callback runs in the driver task. It never blocks on mount I/O and never
 * relies on a finite queue whose disconnect edge could be dropped. */
static void msc_event_cb(const msc_host_event_t *event, void *arg)
{
    (void)arg;
    if (!event) {
        return;
    }

    if (event->event == MSC_DEVICE_CONNECTED) {
        publish_desired_connection(true, event->device.address);
    } else if (event->event == MSC_DEVICE_DISCONNECTED) {
        publish_desired_connection(false, 0u);
    }
}

static void usb_lib_task(void *arg)
{
    (void)arg;
    const usb_host_config_t host_cfg = {
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
        .root_port_unpowered = true,
    };
    ESP_ERROR_CHECK(usb_host_install(&host_cfg));

    const msc_host_driver_config_t msc_cfg = {
        .create_backround_task = true,
        .task_priority = MSC_TASK_PRIO,
        .stack_size = MSC_TASK_STACK,
        .callback = msc_event_cb,
    };
    ESP_ERROR_CHECK(msc_host_install(&msc_cfg));

    root_port_power_cycle("initial bring-up");
    ESP_LOGI(TAG, "USB host + MSC installed; waiting for a drive on the HS USB port");

    uint32_t cycles = 1u;
    TickType_t last_cycle = xTaskGetTickCount();

    for (;;) {
        uint32_t flags = 0u;
        esp_err_t rc = usb_host_lib_handle_events(pdMS_TO_TICKS(RECONCILE_POLL_MS),
                                                  &flags);
        if (rc == ESP_OK && (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS)) {
            usb_host_device_free_all();
        }

        bool seen;
        portENTER_CRITICAL(&s_state_mux);
        seen = s_seen_device;
        portEXIT_CRITICAL(&s_state_mux);
        if (seen) {
            continue;
        }

        TickType_t wait = pdMS_TO_TICKS(
            cycles < ROOT_PORT_MAX_CYCLES ? ROOT_PORT_RETRY_MS : ROOT_PORT_SLOW_MS);
        if (xTaskGetTickCount() - last_cycle >= wait) {
            cycles++;
            last_cycle = xTaskGetTickCount();
            root_port_power_cycle("no device seen yet");
        }
    }
}

static bool wait_for_stable_connection(uint32_t epoch, uint8_t dev_addr)
{
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(CONNECT_STABLE_MS);
    for (;;) {
        if (!desired_matches(epoch, dev_addr)) {
            return false;
        }
        TickType_t now = xTaskGetTickCount();
        if ((int32_t)(deadline - now) <= 0) {
            return true;
        }
        TickType_t remaining = deadline - now;
        (void)ulTaskNotifyTake(pdTRUE, remaining);
        /* Any connect/disconnect notification changes epoch. Re-evaluate rather
         * than mounting an address that was stable only in the past. */
    }
}

static void log_device_info(const char *prefix)
{
    msc_host_device_info_t info = {0};
    if (!s_msc_dev || msc_host_get_device_info(s_msc_dev, &info) != ESP_OK) {
        return;
    }
    uint64_t mb = ((uint64_t)info.sector_size * info.sector_count) /
                  (1024u * 1024u);
    ESP_LOGI(TAG, "%s: %llu MB, sector=%u bytes (VID:0x%04X PID:0x%04X)",
             prefix,
             (unsigned long long)mb,
             (unsigned)info.sector_size,
             info.idVendor,
             info.idProduct);
}

static void log_mount_layout(void)
{
    usb_media_mount_info_t mount_info;
    if (s_mount && usb_media_mount_get_info(s_mount, &mount_info)) {
        ESP_LOGI(TAG,
                 "USB media mounted: base_lba=%u sectors=%u sector_size=%u exfat=%u gpt=%u",
                 (unsigned)mount_info.base_lba,
                 (unsigned)mount_info.sector_count,
                 (unsigned)mount_info.sector_size,
                 mount_info.exfat ? 1u : 0u,
                 mount_info.gpt ? 1u : 0u);
    }

    DIR *dir = opendir(USB_STORAGE_MOUNT_POINT);
    if (!dir) {
        ESP_LOGW(TAG, "opendir(%s) failed: %s",
                 USB_STORAGE_MOUNT_POINT, strerror(errno));
        return;
    }
    struct dirent *entry;
    int count = 0;
    while ((entry = readdir(dir)) != NULL && count < 24) {
        ESP_LOGI(TAG, "  /usb/%s", entry->d_name);
        count++;
    }
    closedir(dir);
}

static esp_err_t mount_desired_device(uint32_t epoch, uint8_t dev_addr)
{
    if (!desired_matches(epoch, dev_addr)) {
        return ESP_ERR_INVALID_STATE;
    }

    /* A previous transient attempt may have left a partially opened handle. */
    if (s_mount || s_msc_dev) {
        release_device();
    }

    ESP_LOGI(TAG, "drive connected (addr=%u), mounting at %s",
             (unsigned)dev_addr, USB_STORAGE_MOUNT_POINT);
    esp_err_t rc = msc_host_install_device(dev_addr, &s_msc_dev);
    if (rc != ESP_OK) {
        ESP_LOGW(TAG, "msc_host_install_device: %s", esp_err_to_name(rc));
        s_msc_dev = NULL;
        return rc;
    }
    if (!desired_matches(epoch, dev_addr)) {
        release_device();
        return ESP_ERR_INVALID_STATE;
    }

    log_device_info("USB MSC device");

    const esp_vfs_fat_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 8192,
    };
    rc = usb_media_mount(s_msc_dev, USB_STORAGE_MOUNT_POINT,
                         &mount_cfg, &s_mount);
    if (rc != ESP_OK) {
        ESP_LOGW(TAG, "usb_media_mount: %s", esp_err_to_name(rc));
        ESP_LOGW(TAG,
                 "mount retry scheduled; supported media is FAT32/exFAT on superfloppy, MBR, or GPT");
        release_device();
        return rc;
    }

    if (!desired_matches(epoch, dev_addr)) {
        release_device();
        return ESP_ERR_INVALID_STATE;
    }

    log_mount_layout();
    log_device_info("mounted");

    if (!commit_mounted(epoch, dev_addr)) {
        release_device();
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

static TickType_t bounded_wait_until(TickType_t deadline)
{
    TickType_t now = xTaskGetTickCount();
    if ((int32_t)(deadline - now) <= 0) {
        return 0;
    }
    TickType_t remaining = deadline - now;
    TickType_t poll = pdMS_TO_TICKS(RECONCILE_POLL_MS);
    return remaining < poll ? remaining : poll;
}

static void storage_task(void *arg)
{
    (void)arg;
    uint32_t retry_ms = MOUNT_RETRY_INITIAL_MS;
    TickType_t next_attempt = 0;

    for (;;) {
        storage_desired_t desired = desired_snapshot();

        if (!desired.connected) {
            bool had_device = s_announced_mounted || s_mount || s_msc_dev;
            if (had_device) {
                ESP_LOGW(TAG, "drive disconnected; reconciling storage state");
            }
            publish_unmounted();
            if (s_mount || s_msc_dev) {
                release_device();
            }
            retry_ms = MOUNT_RETRY_INITIAL_MS;
            next_attempt = 0;
            (void)ulTaskNotifyTake(pdTRUE,
                                  pdMS_TO_TICKS(RECONCILE_POLL_MS));
            continue;
        }

        if (usb_storage_is_mounted()) {
            (void)ulTaskNotifyTake(pdTRUE,
                                  pdMS_TO_TICKS(RECONCILE_POLL_MS));
            continue;
        }

        TickType_t wait = bounded_wait_until(next_attempt);
        if (wait > 0) {
            (void)ulTaskNotifyTake(pdTRUE, wait);
            continue;
        }

        desired = desired_snapshot();
        if (!desired.connected ||
            !wait_for_stable_connection(desired.epoch, desired.dev_addr)) {
            continue;
        }

        esp_err_t rc = mount_desired_device(desired.epoch, desired.dev_addr);
        if (rc == ESP_OK) {
            retry_ms = MOUNT_RETRY_INITIAL_MS;
            next_attempt = 0;
            continue;
        }
        if (!desired_matches(desired.epoch, desired.dev_addr)) {
            retry_ms = MOUNT_RETRY_INITIAL_MS;
            next_attempt = 0;
            continue;
        }

        ESP_LOGW(TAG, "USB mount attempt failed (%s); retrying in %u ms",
                 esp_err_to_name(rc), (unsigned)retry_ms);
        next_attempt = xTaskGetTickCount() + pdMS_TO_TICKS(retry_ms);
        if (retry_ms < MOUNT_RETRY_MAX_MS) {
            uint32_t doubled = retry_ms * 2u;
            retry_ms = doubled > MOUNT_RETRY_MAX_MS
                           ? MOUNT_RETRY_MAX_MS
                           : doubled;
        }
    }
}

esp_err_t usb_storage_init(usb_storage_event_cb_t cb)
{
    if (s_storage_task || s_usb_lib_task) {
        return ESP_ERR_INVALID_STATE;
    }

    media_io_gate_set_available(false);
    s_cb = cb;
    portENTER_CRITICAL(&s_state_mux);
    memset(&s_desired, 0, sizeof(s_desired));
    s_mounted = false;
    s_announced_mounted = false;
    s_seen_device = false;
    portEXIT_CRITICAL(&s_state_mux);

    if (xTaskCreate(storage_task, "usb_store", STORAGE_TASK_STACK,
                    NULL, STORAGE_TASK_PRIO, &s_storage_task) != pdPASS) {
        s_storage_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(usb_lib_task, "usb_lib", USB_LIB_TASK_STACK,
                    NULL, USB_LIB_TASK_PRIO, &s_usb_lib_task) != pdPASS) {
        vTaskDelete(s_storage_task);
        s_storage_task = NULL;
        s_usb_lib_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
