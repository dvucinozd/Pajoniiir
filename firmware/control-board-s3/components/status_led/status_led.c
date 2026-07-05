#include "status_led.h"

#include <stdatomic.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

// Onboard user LED on the Seeed Studio XIAO ESP32S3 / Sense. It is active-low.
#define STATUS_LED_GPIO        21
#define STATUS_LED_TICK_US     (60 * 1000)

// P4 sends the VU-meter LED stream continuously (~66 frames/s), so a few
// seconds of silence reliably means the UART link is down.
#define STATUS_LED_P4_TIMEOUT_MS 3000u

static const char *TAG = "status_led";

static esp_timer_handle_t s_timer;
static atomic_bool s_initialized;
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
    if (!atomic_load(&s_initialized)) {
        return;
    }

    const bool connected = atomic_load(&s_connected);
    const bool flash = atomic_exchange(&s_activity, 0u) != 0u;
    const bool active = !p4_link_up() || connected || flash;
    (void)gpio_set_level(STATUS_LED_GPIO, active ? 0 : 1);
}

esp_err_t status_led_init(void)
{
    if (atomic_load(&s_initialized)) {
        return ESP_OK;
    }

    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << STATUS_LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t rc = gpio_config(&cfg);
    if (rc != ESP_OK) {
        ESP_LOGW(TAG, "GPIO init failed: %s", esp_err_to_name(rc));
        return rc;
    }
    (void)gpio_set_level(STATUS_LED_GPIO, 1);

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
        (void)gpio_set_level(STATUS_LED_GPIO, 1);
        return rc;
    }

    atomic_store(&s_connected, false);
    atomic_store(&s_initialized, true);
    status_led_render_cb(NULL);   /* show boot/P4-link status immediately */
    ESP_LOGI(TAG, "XIAO ESP32S3 user LED on GPIO%d (active-low)", STATUS_LED_GPIO);
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
