#include "status_led.h"

#include <stdatomic.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "led_strip.h"

// Onboard addressable RGB LED on the ESP32-S3-DevKitC-1 (N16R8).
#define STATUS_LED_GPIO        48
#define STATUS_LED_TICK_US     (60 * 1000)

// The onboard WS2812 is bright; keep the base level dim.
#define STATUS_LED_LEVEL_BASE  16u
#define STATUS_LED_LEVEL_FLASH 48u

// P4 sends the VU-meter LED stream continuously (~66 frames/s), so a few
// seconds of silence reliably means the UART link is down.
#define STATUS_LED_P4_TIMEOUT_MS 3000u

static const char *TAG = "status_led";

static led_strip_handle_t s_strip;
static esp_timer_handle_t s_timer;
static atomic_bool s_connected;
static atomic_uint s_activity;
static atomic_uint s_p4_last_frame_ms;   /* 0 = no frame seen yet (boot) */

static bool p4_link_up(void)
{
    const uint32_t last_ms = atomic_load(&s_p4_last_frame_ms);
    if (last_ms == 0u) {
        return false;
    }
    const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    return (now_ms - last_ms) < STATUS_LED_P4_TIMEOUT_MS;
}

static void status_led_render_cb(void *arg)
{
    (void)arg;
    if (!s_strip) {
        return;
    }

    const bool connected = atomic_load(&s_connected);
    const bool flash = atomic_exchange(&s_activity, 0u) != 0u;

    uint8_t red = 0;
    uint8_t green = 0;
    if (!p4_link_up()) {
        red = STATUS_LED_LEVEL_BASE;                 /* amber */
        green = STATUS_LED_LEVEL_BASE / 3u;
    } else if (connected) {
        green = flash ? STATUS_LED_LEVEL_FLASH : STATUS_LED_LEVEL_BASE;
    } else {
        red = STATUS_LED_LEVEL_BASE;
    }
    (void)led_strip_set_pixel(s_strip, 0, red, green, 0);
    (void)led_strip_refresh(s_strip);
}

esp_err_t status_led_init(void)
{
    if (s_strip) {
        return ESP_OK;
    }

    led_strip_config_t strip_cfg = {
        .strip_gpio_num = STATUS_LED_GPIO,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
    };
    led_strip_rmt_config_t rmt_cfg = {
        .resolution_hz = 10 * 1000 * 1000,
    };
    esp_err_t rc = led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip);
    if (rc != ESP_OK) {
        ESP_LOGW(TAG, "led_strip init failed: %s", esp_err_to_name(rc));
        return rc;
    }

    const esp_timer_create_args_t timer_args = {
        .callback = status_led_render_cb,
        .name = "status_led",
    };
    rc = esp_timer_create(&timer_args, &s_timer);
    if (rc == ESP_OK) {
        rc = esp_timer_start_periodic(s_timer, STATUS_LED_TICK_US);
    }
    if (rc != ESP_OK) {
        if (s_timer) {
            (void)esp_timer_delete(s_timer);
            s_timer = NULL;
        }
        (void)led_strip_del(s_strip);
        s_strip = NULL;
        return rc;
    }

    atomic_store(&s_connected, false);
    status_led_render_cb(NULL);   /* show red immediately, not after one tick */
    ESP_LOGI(TAG, "onboard RGB status LED on GPIO%d", STATUS_LED_GPIO);
    return ESP_OK;
}

void status_led_set_connected(bool connected)
{
    atomic_store(&s_connected, connected);
}

void status_led_notify_activity(void)
{
    atomic_fetch_add(&s_activity, 1u);
}

void status_led_notify_p4_frame(void)
{
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    if (now_ms == 0u) {
        now_ms = 1u;   /* keep 0 reserved for "never seen a frame" */
    }
    atomic_store(&s_p4_last_frame_ms, now_ms);
}
