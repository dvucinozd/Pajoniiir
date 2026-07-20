#include "sd_io_gate.h"

/* Pure admission policy — shared by firmware and host test builds. */
bool sd_io_gate_admit(sd_io_class_t op_class, bool recorder_active)
{
    if (!recorder_active) {
        return true;
    }
    switch (op_class) {
    case SD_IO_CLASS_PROFILE_UPLOAD:
    case SD_IO_CLASS_LOG_DOWNLOAD:
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
    return s_recorder_active;
}

#else

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "stdatomic.h"

static const char *TAG = "sd_io_gate";
static SemaphoreHandle_t s_gate;
static atomic_bool s_recorder_active;

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
    xSemaphoreTake(s_gate, portMAX_DELAY);
}

bool sd_io_gate_try_begin(uint32_t timeout_ms)
{
    if (!s_gate) {
        if (sd_io_gate_init() != ESP_OK) {
            return false;
        }
    }
    return xSemaphoreTake(s_gate, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void sd_io_gate_end(void)
{
    if (s_gate) {
        xSemaphoreGive(s_gate);
    }
}

void sd_io_gate_set_recorder_active(bool active)
{
    atomic_store_explicit(&s_recorder_active, active, memory_order_release);
}

bool sd_io_gate_recorder_active(void)
{
    return atomic_load_explicit(&s_recorder_active, memory_order_acquire);
}

#endif
