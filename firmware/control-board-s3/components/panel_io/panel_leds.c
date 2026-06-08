#include "panel_io.h"
#include "panel_io_priv.h"
#include "esp_check.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "panel_leds";

// ─── Pin assignment ───────────────────────────────────────────────────────────
// Adjust these to match your actual wiring before soldering the final harness.
// See docs/wiring-map.md for the signal definitions.
#define PIN_LED_CUE  GPIO_NUM_33
#define PIN_LED_PLAY GPIO_NUM_34
#define PIN_LED_BEAT GPIO_NUM_38
#define PIN_LED_END  GPIO_NUM_39

static const gpio_num_t LED_PINS[LED_COUNT] = {
    [LED_CUE]  = PIN_LED_CUE,
    [LED_PLAY] = PIN_LED_PLAY,
    [LED_BEAT] = PIN_LED_BEAT,
    [LED_END]  = PIN_LED_END,
};

typedef struct {
    bool     on;
    uint32_t blink_period_ms;  // 0 = not blinking
    uint32_t blink_elapsed_ms;
} led_state_t;

static led_state_t s_leds[LED_COUNT];
static SemaphoreHandle_t s_mutex;

esp_err_t panel_leds_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        return ESP_ERR_NO_MEM;
    }

    gpio_config_t cfg = {
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
        .pin_bit_mask = (1ULL << PIN_LED_CUE)  |
                        (1ULL << PIN_LED_PLAY) |
                        (1ULL << PIN_LED_BEAT) |
                        (1ULL << PIN_LED_END),
    };
    ESP_RETURN_ON_ERROR(gpio_config(&cfg), TAG, "gpio config");

    for (int i = 0; i < LED_COUNT; i++) {
        ESP_RETURN_ON_ERROR(gpio_set_level(LED_PINS[i], 0), TAG, "led off");
    }
    return ESP_OK;
}

void panel_led_set(led_id_t led, bool on)
{
    if (led >= LED_COUNT) return;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_leds[led].on             = on;
    s_leds[led].blink_period_ms = 0;
    gpio_set_level(LED_PINS[led], on ? 1 : 0);
    xSemaphoreGive(s_mutex);
}

void panel_led_blink(led_id_t led, uint32_t period_ms)
{
    if (led >= LED_COUNT) return;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_leds[led].blink_period_ms  = period_ms;
    s_leds[led].blink_elapsed_ms = 0;
    if (period_ms == 0) {
        gpio_set_level(LED_PINS[led], 0);
        s_leds[led].on = false;
    }
    xSemaphoreGive(s_mutex);
}

void panel_led_tick(uint32_t elapsed_ms)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    for (int i = 0; i < LED_COUNT; i++) {
        if (s_leds[i].blink_period_ms == 0) continue;

        s_leds[i].blink_elapsed_ms += elapsed_ms;
        if (s_leds[i].blink_elapsed_ms >= s_leds[i].blink_period_ms) {
            s_leds[i].blink_elapsed_ms = 0;
            s_leds[i].on = !s_leds[i].on;
            gpio_set_level(LED_PINS[i], s_leds[i].on ? 1 : 0);
        }
    }
    xSemaphoreGive(s_mutex);
}
