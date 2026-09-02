#include "usb_storage.h"
#include "usb_storage_recovery.h"
#include "usb_storage_session.h"
#include "media_io_gate.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "usb/usb_host.h"
#include "usb/msc_host.h"
#include "usb_media_mount.h"

#include <dirent.h>
#include <errno.h>
#include <stdatomic.h>
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

#ifndef USB_STORAGE_DEVICE_ROUTE_ALLOWED
#define USB_STORAGE_DEVICE_ROUTE_ALLOWED(address) (true)
#endif

#ifndef USB_STORAGE_REQUEST_ROOT_RECOVERY
#define USB_STORAGE_REQUEST_ROOT_RECOVERY(why) (false)
#endif

static TaskHandle_t             s_storage_task;
static TaskHandle_t             s_usb_lib_task;
static usb_storage_event_cb_t   s_cb;
static msc_host_device_handle_t s_msc_dev;
static usb_media_mount_t       *s_mount;

/* Driver callback publishes only desired state. The storage task is the sole
 * owner of mount/unmount handles and callback publication. */
static portMUX_TYPE s_state_mux = portMUX_INITIALIZER_UNLOCKED;
static usb_storage_session_t s_session;
static bool s_announced_mounted;
static atomic_uint_fast32_t s_connect_events;
static atomic_uint_fast32_t s_connect_accepted;
static atomic_uint_fast32_t s_disconnect_events;
static atomic_uint_fast32_t s_disconnect_accepted;
static atomic_uint_fast32_t s_mount_attempts;
static atomic_uint_fast32_t s_mount_successes;
static atomic_int s_last_mount_result;
static atomic_uint_fast32_t s_releases;
static atomic_int s_last_unmount_result;
static atomic_int s_last_uninstall_result;

static usb_storage_session_t desired_snapshot(void)
{
    usb_storage_session_t snapshot;
    portENTER_CRITICAL(&s_state_mux);
    snapshot = s_session;
    portEXIT_CRITICAL(&s_state_mux);
    return snapshot;
}

static bool desired_matches(uint32_t epoch, uint8_t dev_addr)
{
    bool matches;
    portENTER_CRITICAL(&s_state_mux);
    matches = usb_storage_session_matches(&s_session, epoch, dev_addr);
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

static void publish_desired_connect(uint8_t dev_addr)
{
    atomic_fetch_add_explicit(&s_connect_events, 1u, memory_order_relaxed);
    usb_storage_connect_result_t result;
    portENTER_CRITICAL(&s_state_mux);
    result = usb_storage_session_on_connect(&s_session, dev_addr);
    portEXIT_CRITICAL(&s_state_mux);

    if (result == USB_STORAGE_CONNECT_IGNORED_SECONDARY) {
        ESP_LOGW(TAG, "second USB storage device ignored (addr=%u)",
                 (unsigned)dev_addr);
        return;
    }
    if (result == USB_STORAGE_CONNECT_ACCEPTED) {
        atomic_fetch_add_explicit(&s_connect_accepted, 1u,
                                  memory_order_relaxed);
        notify_storage_owner();
    }
}

static void publish_desired_disconnect(msc_host_device_handle_t handle)
{
    atomic_fetch_add_explicit(&s_disconnect_events, 1u,
                              memory_order_relaxed);
    usb_storage_disconnect_result_t result;
    portENTER_CRITICAL(&s_state_mux);
    result = usb_storage_session_on_disconnect(
        &s_session, (uintptr_t)handle);
    portEXIT_CRITICAL(&s_state_mux);

    if (result == USB_STORAGE_DISCONNECT_IGNORED_FOREIGN) {
        ESP_LOGW(TAG, "disconnect for non-owner USB storage handle ignored");
        return;
    }
    if (result == USB_STORAGE_DISCONNECT_ALREADY_INACTIVE) {
        return;
    }

    atomic_fetch_add_explicit(&s_disconnect_accepted, 1u,
                              memory_order_relaxed);

    /* Disconnect is level state, not a lossy edge. Block new reads immediately;
     * the owner task performs the safe unmount after gate holders drain. */
    media_io_gate_set_available(false);
    notify_storage_owner();
}

static void root_port_power_cycle(const char *why)
{
    ESP_LOGI(TAG, "root port power cycle (%s)", why ? why : "");
    if (USB_STORAGE_REQUEST_ROOT_RECOVERY(why)) {
        return;
    }
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
    /* Detach the sole-owner handles before entering either teardown routine.
     * A DEV_GONE edge can be published while the MSC/VFS layers are being
     * dismantled; no reconcile pass may observe and release the same handles
     * twice. */
    usb_media_mount_t *released_mount = s_mount;
    msc_host_device_handle_t released_handle = s_msc_dev;
    s_mount = NULL;
    s_msc_dev = NULL;
    atomic_fetch_add_explicit(&s_releases, 1u, memory_order_relaxed);

    if (released_handle) {
        portENTER_CRITICAL(&s_state_mux);
        usb_storage_session_release_handle(
            &s_session, (uintptr_t)released_handle);
        portEXIT_CRITICAL(&s_state_mux);
    }

    media_io_gate_begin();
    if (released_mount) {
        esp_err_t rc = usb_media_unmount(released_mount);
        atomic_store_explicit(&s_last_unmount_result, rc,
                              memory_order_relaxed);
        if (rc != ESP_OK) {
            ESP_LOGW(TAG, "usb_media_unmount: %s", esp_err_to_name(rc));
        }
    }
    if (released_handle) {
        esp_err_t rc = msc_host_uninstall_device(released_handle);
        atomic_store_explicit(&s_last_uninstall_result, rc,
                              memory_order_relaxed);
        if (rc != ESP_OK) {
            ESP_LOGW(TAG, "msc_host_uninstall_device: %s", esp_err_to_name(rc));
        }
    }
    media_io_gate_end();
}

static void publish_unmounted(void)
{
    bool notify = false;
    portENTER_CRITICAL(&s_state_mux);
    usb_storage_session_mark_unmounted(&s_session);
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
    if (usb_storage_session_commit_mounted(
            &s_session, epoch, dev_addr)) {
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
    mounted = s_session.mounted;
    portEXIT_CRITICAL(&s_state_mux);
    return mounted;
}

void usb_storage_get_diagnostics(usb_storage_diagnostics_t *out)
{
    if (!out) {
        return;
    }

    usb_storage_session_t session = desired_snapshot();
    *out = (usb_storage_diagnostics_t) {
        .desired_connected = session.connected,
        .mounted = session.mounted,
        .connect_events = (uint32_t)atomic_load_explicit(
            &s_connect_events, memory_order_relaxed),
        .connect_accepted = (uint32_t)atomic_load_explicit(
            &s_connect_accepted, memory_order_relaxed),
        .disconnect_events = (uint32_t)atomic_load_explicit(
            &s_disconnect_events, memory_order_relaxed),
        .disconnect_accepted = (uint32_t)atomic_load_explicit(
            &s_disconnect_accepted, memory_order_relaxed),
        .mount_attempts = (uint32_t)atomic_load_explicit(
            &s_mount_attempts, memory_order_relaxed),
        .mount_successes = (uint32_t)atomic_load_explicit(
            &s_mount_successes, memory_order_relaxed),
        .last_mount_result = (esp_err_t)atomic_load_explicit(
            &s_last_mount_result, memory_order_relaxed),
        .releases = (uint32_t)atomic_load_explicit(
            &s_releases, memory_order_relaxed),
        .last_unmount_result = (esp_err_t)atomic_load_explicit(
            &s_last_unmount_result, memory_order_relaxed),
        .last_uninstall_result = (esp_err_t)atomic_load_explicit(
            &s_last_uninstall_result, memory_order_relaxed),
    };
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
        publish_desired_connect(event->device.address);
    } else if (event->event == MSC_DEVICE_DISCONNECTED) {
        publish_desired_disconnect(event->device.handle);
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

    usb_storage_session_t session = desired_snapshot();
    usb_storage_recovery_t recovery;
    usb_storage_recovery_init(&recovery,
                              session.connected,
                              session.epoch,
                              (uint32_t)xTaskGetTickCount(),
                              1u);

    for (;;) {
        uint32_t flags = 0u;
        esp_err_t rc = usb_host_lib_handle_events(pdMS_TO_TICKS(RECONCILE_POLL_MS),
                                                  &flags);
        if (rc == ESP_OK && (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS)) {
            usb_host_device_free_all();
        }

        session = desired_snapshot();
        uint32_t now = (uint32_t)xTaskGetTickCount();
        usb_storage_recovery_observe(&recovery,
                                     session.connected,
                                     session.epoch,
                                     now);
        if (!usb_storage_recovery_cycle_due(
                &recovery,
                now,
                (uint32_t)pdMS_TO_TICKS(ROOT_PORT_RETRY_MS),
                ROOT_PORT_MAX_CYCLES,
                (uint32_t)pdMS_TO_TICKS(ROOT_PORT_SLOW_MS))) {
            continue;
        }

        bool was_slow = usb_storage_recovery_uses_slow_cadence(
            &recovery, ROOT_PORT_MAX_CYCLES);
        usb_storage_recovery_mark_cycle(&recovery, now);
        bool now_slow = usb_storage_recovery_uses_slow_cadence(
            &recovery, ROOT_PORT_MAX_CYCLES);
        if (!was_slow && now_slow) {
            ESP_LOGW(TAG,
                     "USB enumeration recovery exhausted %u fast cycles; "
                     "continuing every %u ms",
                     (unsigned)ROOT_PORT_MAX_CYCLES,
                     (unsigned)ROOT_PORT_SLOW_MS);
        }
        root_port_power_cycle("no active storage session");
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
    atomic_fetch_add_explicit(&s_mount_attempts, 1u, memory_order_relaxed);
    if (!desired_matches(epoch, dev_addr)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!USB_STORAGE_DEVICE_ROUTE_ALLOWED(dev_addr)) {
        atomic_store_explicit(&s_last_mount_result, ESP_ERR_NOT_SUPPORTED,
                              memory_order_relaxed);
        ESP_LOGW(TAG, "rejecting MSC addr=%u outside the USB0 direct root",
                 (unsigned)dev_addr);
        return ESP_ERR_NOT_SUPPORTED;
    }

    /* A previous transient attempt may have left a partially opened handle. */
    if (s_mount || s_msc_dev) {
        release_device();
    }

    ESP_LOGI(TAG, "drive connected (addr=%u), mounting at %s",
             (unsigned)dev_addr, USB_STORAGE_MOUNT_POINT);
    esp_err_t rc = msc_host_install_device(dev_addr, &s_msc_dev);
    if (rc != ESP_OK) {
        atomic_store_explicit(&s_last_mount_result, rc,
                              memory_order_relaxed);
        ESP_LOGW(TAG, "msc_host_install_device: %s", esp_err_to_name(rc));
        s_msc_dev = NULL;
        return rc;
    }
    if (!desired_matches(epoch, dev_addr)) {
        release_device();
        return ESP_ERR_INVALID_STATE;
    }

    bool handle_bound;
    portENTER_CRITICAL(&s_state_mux);
    handle_bound = usb_storage_session_bind_handle(
        &s_session, epoch, dev_addr, (uintptr_t)s_msc_dev);
    portEXIT_CRITICAL(&s_state_mux);
    if (!handle_bound) {
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
        atomic_store_explicit(&s_last_mount_result, rc,
                              memory_order_relaxed);
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
        atomic_store_explicit(&s_last_mount_result, ESP_ERR_INVALID_STATE,
                              memory_order_relaxed);
        release_device();
        return ESP_ERR_INVALID_STATE;
    }
    atomic_fetch_add_explicit(&s_mount_successes, 1u, memory_order_relaxed);
    atomic_store_explicit(&s_last_mount_result, ESP_OK,
                          memory_order_relaxed);
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
        usb_storage_session_t desired = desired_snapshot();

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
    usb_storage_session_reset(&s_session);
    s_announced_mounted = false;
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
