#include "media_io_gate.h"

#if defined(MEDIA_IO_GATE_STANDALONE_TEST)

static bool s_locked;
static bool s_available;

esp_err_t media_io_gate_init(void)
{
    s_locked = false;
    s_available = false;
    return ESP_OK;
}

void media_io_gate_begin(void)
{
    while (s_locked) {
    }
    s_locked = true;
}

bool media_io_gate_try_begin(uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (s_locked) {
        return false;
    }
    s_locked = true;
    return true;
}

void media_io_gate_end(void)
{
    s_locked = false;
}

void media_io_gate_set_available(bool available)
{
    s_available = available;
}

bool media_io_gate_is_available(void)
{
    return s_available;
}

#else

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "stdatomic.h"

static const char *TAG = "media_io_gate";
static SemaphoreHandle_t s_gate;
static atomic_bool s_available;

esp_err_t media_io_gate_init(void)
{
    if (s_gate) {
        return ESP_OK;
    }
    s_gate = xSemaphoreCreateMutex();
    if (!s_gate) {
        ESP_LOGE(TAG, "failed to create USB media I/O mutex");
        return ESP_FAIL;
    }
    return ESP_OK;
}

void media_io_gate_begin(void)
{
    if (!s_gate) {
        if (media_io_gate_init() != ESP_OK) {
            return;
        }
    }
    xSemaphoreTake(s_gate, portMAX_DELAY);
}

bool media_io_gate_try_begin(uint32_t timeout_ms)
{
    if (!s_gate) {
        if (media_io_gate_init() != ESP_OK) {
            return false;
        }
    }
    return xSemaphoreTake(s_gate, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void media_io_gate_end(void)
{
    if (s_gate) {
        xSemaphoreGive(s_gate);
    }
}

void media_io_gate_set_available(bool available)
{
    atomic_store_explicit(&s_available, available, memory_order_release);
}

bool media_io_gate_is_available(void)
{
    return atomic_load_explicit(&s_available, memory_order_acquire);
}

#endif
