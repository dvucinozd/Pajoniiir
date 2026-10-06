#include "sd_io_gate.h"
#include <stddef.h>

static unsigned s_activity;
bool sd_io_gate_reserve(sd_io_activity_t activity)
{
    if (activity != SD_ACTIVITY_RECORDER && activity != SD_ACTIVITY_DOWNLOAD) return false;
    unsigned expected = SD_ACTIVITY_NONE;
    return __atomic_compare_exchange_n(&s_activity, &expected, (unsigned)activity,
                                      false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}
void sd_io_gate_release(sd_io_activity_t activity)
{
    if (activity != SD_ACTIVITY_RECORDER && activity != SD_ACTIVITY_DOWNLOAD) return;
    unsigned expected = (unsigned)activity;
    (void)__atomic_compare_exchange_n(&s_activity, &expected, SD_ACTIVITY_NONE,
                                      false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}
sd_io_activity_t sd_io_gate_activity(void)
{
    return (sd_io_activity_t)__atomic_load_n(&s_activity, __ATOMIC_ACQUIRE);
}
static sd_io_gate_stats_t s_stats;
void sd_io_gate_get_stats(sd_io_gate_stats_t *out)
{
    if (!out) return;
    out->waits = __atomic_load_n(&s_stats.waits, __ATOMIC_RELAXED);
    out->timeouts = __atomic_load_n(&s_stats.timeouts, __ATOMIC_RELAXED);
    out->max_wait_us = __atomic_load_n(&s_stats.max_wait_us, __ATOMIC_RELAXED);
    out->max_hold_us = __atomic_load_n(&s_stats.max_hold_us, __ATOMIC_RELAXED);
}

/* Pure admission policy — shared by firmware and host test builds. */
bool sd_io_gate_admit(sd_io_class_t op_class, bool recorder_active)
{
    if (!recorder_active) {
        return true;
    }
    switch (op_class) {
    case SD_IO_CLASS_PROFILE_UPLOAD:
    case SD_IO_CLASS_LOG_DOWNLOAD:
    case SD_IO_CLASS_TRACK_DOWNLOAD:
        return false;   /* defer heavy optional admin work during recording */
    default:
        return true;    /* bounded fast operations always proceed */
    }
}

#if defined(SD_IO_GATE_STANDALONE_TEST)

static bool s_locked;
static bool s_recorder_active;

esp_err_t sd_io_gate_init(void)
{
    s_locked = false;
    s_recorder_active = false;
    return ESP_OK;
}

void sd_io_gate_begin(void)
{
    while (s_locked) {
    }
    s_locked = true;
}

bool sd_io_gate_try_begin(uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (s_locked) {
        return false;
    }
    s_locked = true;
    return true;
}

void sd_io_gate_end(void)
{
    s_locked = false;
}

void sd_io_gate_set_recorder_active(bool active)
{
    s_recorder_active = active;
}

bool sd_io_gate_recorder_active(void)
{
    return s_recorder_active || sd_io_gate_activity() == SD_ACTIVITY_RECORDER;
}

#else

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "stdatomic.h"
#include "esp_timer.h"
#include <limits.h>

static const char *TAG = "sd_io_gate";
static SemaphoreHandle_t s_gate;
static atomic_bool s_recorder_active;
static int64_t s_acquired_us;
static void note_max(uint32_t *counter, int64_t us)
{
    uint32_t value = us <= 0 ? 0 : ((uint64_t)us > UINT32_MAX ? UINT32_MAX : (uint32_t)us);
    uint32_t old = __atomic_load_n(counter, __ATOMIC_RELAXED);
    while (value > old && !__atomic_compare_exchange_n(counter, &old, value,
            false, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {}
}
static void note_inc(uint32_t *counter)
{
    uint32_t old = __atomic_load_n(counter, __ATOMIC_RELAXED);
    while (old != UINT32_MAX && !__atomic_compare_exchange_n(counter, &old,
            old + 1u, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {}
}

esp_err_t sd_io_gate_init(void)
{
    if (s_gate) {
        return ESP_OK;
    }
    s_gate = xSemaphoreCreateMutex();
    if (!s_gate) {
        ESP_LOGE(TAG, "failed to create SD I/O mutex");
        return ESP_FAIL;
    }
    return ESP_OK;
}

void sd_io_gate_begin(void)
{
    if (!s_gate) {
        if (sd_io_gate_init() != ESP_OK) {
            return;
        }
    }
    int64_t started = esp_timer_get_time();
    xSemaphoreTake(s_gate, portMAX_DELAY);
    s_acquired_us = esp_timer_get_time();
    note_inc(&s_stats.waits);
    note_max(&s_stats.max_wait_us, s_acquired_us - started);
}

bool sd_io_gate_try_begin(uint32_t timeout_ms)
{
    if (!s_gate) {
        if (sd_io_gate_init() != ESP_OK) {
            return false;
        }
    }
    int64_t started = esp_timer_get_time();
    bool acquired = xSemaphoreTake(s_gate, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
    int64_t now = esp_timer_get_time();
    note_inc(&s_stats.waits);
    note_max(&s_stats.max_wait_us, now - started);
    if (acquired) s_acquired_us = now;
    else note_inc(&s_stats.timeouts);
    return acquired;
}

void sd_io_gate_end(void)
{
    if (s_gate) {
        note_max(&s_stats.max_hold_us, esp_timer_get_time() - s_acquired_us);
        xSemaphoreGive(s_gate);
    }
}

void sd_io_gate_set_recorder_active(bool active)
{
    atomic_store_explicit(&s_recorder_active, active, memory_order_release);
}

bool sd_io_gate_recorder_active(void)
{
    return atomic_load_explicit(&s_recorder_active, memory_order_acquire) ||
           sd_io_gate_activity() == SD_ACTIVITY_RECORDER;
}

#endif
