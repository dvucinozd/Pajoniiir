#include "media_io_gate.h"

#if defined(MEDIA_IO_GATE_STANDALONE_TEST)

static bool s_locked;

esp_err_t media_io_gate_init(void)
{
    s_locked = false;
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

#else

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "media_io_gate";
static SemaphoreHandle_t s_gate;

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

#endif
