/* Production wrapper: debounce brightness persistence outside the LVGL task. */
#include "freertos/task.h"

#define app_settings_init          app_settings_init_legacy
#define app_settings_set_backlight app_settings_set_backlight_legacy_immediate
#include "app_settings.c"
#undef app_settings_init
#undef app_settings_set_backlight

#define BACKLIGHT_DEBOUNCE_MS 500u
#define SETTINGS_WORKER_STACK 3072u
#define SETTINGS_WORKER_PRIO  2u

static TaskHandle_t s_settings_worker;
static uint8_t s_pending_backlight = 80u;

static void settings_persist_worker(void *arg)
{
    (void)arg;
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        /* Each new VALUE_CHANGED notification restarts the quiet interval. */
        while (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(BACKLIGHT_DEBOUNCE_MS)) != 0u) {
        }

        const uint8_t value = __atomic_load_n(&s_pending_backlight, __ATOMIC_ACQUIRE);
        if (save_u8("backlight", value) == ESP_OK) {
            portENTER_CRITICAL(&s_cfg_mux);
            s_cfg.backlight_pct = value;
            portEXIT_CRITICAL(&s_cfg_mux);
            ESP_LOGI(TAG, "backlight persisted after debounce: %u%%", (unsigned)value);
        }
    }
}

esp_err_t app_settings_init(void)
{
    esp_err_t rc = app_settings_init_legacy();
    if (rc != ESP_OK) return rc;

    app_settings_t snapshot = app_settings_get();
    __atomic_store_n(&s_pending_backlight, snapshot.backlight_pct, __ATOMIC_RELEASE);
    if (!s_settings_worker &&
        xTaskCreate(settings_persist_worker,
                    "settings_nvs",
                    SETTINGS_WORKER_STACK,
                    NULL,
                    SETTINGS_WORKER_PRIO,
                    &s_settings_worker) != pdPASS) {
        s_settings_worker = NULL;
        ESP_LOGE(TAG, "failed to create settings persistence worker");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void app_settings_set_backlight(uint8_t value)
{
    if (value > 100u) value = 100u;
    __atomic_store_n(&s_pending_backlight, value, __ATOMIC_RELEASE);

    /* Tests or unusually early calls may precede app_settings_init(). Preserve
     * correctness with the checked synchronous path in that exceptional case. */
    TaskHandle_t worker = __atomic_load_n(&s_settings_worker, __ATOMIC_ACQUIRE);
    if (!worker) {
        app_settings_set_backlight_legacy_immediate(value);
        return;
    }
    xTaskNotifyGive(worker);
}
