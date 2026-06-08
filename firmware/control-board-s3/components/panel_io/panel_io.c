#include "panel_io.h"
#include "panel_io_priv.h"
#include "esp_check.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <inttypes.h>
#include <string.h>

static const char *TAG = "panel_io";

// ─── Pin assignment ───────────────────────────────────────────────────────────
// Adjust before wiring the CDJ front panel. See docs/wiring-map.md.
// All buttons are active-low (pulled to GND when pressed).
//
// Avoided: GPIO0 (boot), GPIO19/20 (USB D-/D+), GPIO35-37 (octal PSRAM on N16R8),
//          GPIO43/44 (UART0 flash/log), GPIO45/46 (strap-sensitive).

static const gpio_num_t BUTTON_PINS[BTN_COUNT] = {
    [BTN_EJECT]        = GPIO_NUM_2,
    [BTN_TRACK_PREV]   = GPIO_NUM_3,
    [BTN_TRACK_NEXT]   = GPIO_NUM_4,
    [BTN_SEARCH_BACK]  = GPIO_NUM_5,
    [BTN_SEARCH_FWD]   = GPIO_NUM_6,
    [BTN_CUE]          = GPIO_NUM_7,
    [BTN_PLAY]         = GPIO_NUM_8,
    [BTN_PERF1]        = GPIO_NUM_9,
    [BTN_PERF2]        = GPIO_NUM_10,
    [BTN_PERF3]        = GPIO_NUM_11,
    [BTN_HOLD]         = GPIO_NUM_12,
    [BTN_MODE]         = GPIO_NUM_13,
    [BTN_MASTER_TEMPO] = GPIO_NUM_14,
    [BTN_LOAD]         = GPIO_NUM_21,
};

// ─── Debounce ─────────────────────────────────────────────────────────────────
// Scan period is SCAN_PERIOD_MS. A button must read stable for DEBOUNCE_TICKS
// consecutive scans before its confirmed state changes.
#define SCAN_PERIOD_MS  5
#define DEBOUNCE_TICKS  3   // 3 × 5 ms = 15 ms

// Performance buttons (Cue, Play, Perf1-3) use a tighter debounce.
#define DEBOUNCE_FAST_TICKS 1  // 1 × 5 ms = 5 ms

static inline int debounce_ticks_for(button_id_t id)
{
    switch (id) {
        case BTN_CUE:
        case BTN_PLAY:
        case BTN_PERF1:
        case BTN_PERF2:
        case BTN_PERF3:
            return DEBOUNCE_FAST_TICKS;
        default:
            return DEBOUNCE_TICKS;
    }
}

typedef struct {
    bool    confirmed;    // last emitted state
    int     stable_cnt;   // consecutive samples matching raw reading
} btn_state_t;

static btn_state_t s_btn[BTN_COUNT];
static QueueHandle_t s_queue;

static uint32_t s_button_drop_count;
static uint32_t s_jog_drop_count;
static uint32_t s_browse_drop_count;
static uint32_t s_pitch_drop_count;
static uint16_t s_pending_pitch;
static bool     s_pending_pitch_valid;
static TickType_t s_last_drop_warn;

static void panel_queue_warn_rate_limited(void)
{
    TickType_t now = xTaskGetTickCount();
    if (now - s_last_drop_warn < pdMS_TO_TICKS(1000)) {
        return;
    }
    s_last_drop_warn = now;
    ESP_LOGW(TAG, "panel queue drops: button=%" PRIu32 " jog=%" PRIu32
             " browse=%" PRIu32 " pitch=%" PRIu32,
             s_button_drop_count, s_jog_drop_count, s_browse_drop_count, s_pitch_drop_count);
}

static bool panel_queue_send(panel_event_t *ev)
{
    if (xQueueSend(s_queue, ev, 0) == pdTRUE) {
        return true;
    }

    if (ev->type == PANEL_EV_BUTTON && ev->value == 0) {
        panel_event_t dropped;
        if (xQueueReceive(s_queue, &dropped, 0) == pdTRUE &&
            xQueueSend(s_queue, ev, 0) == pdTRUE) {
            s_button_drop_count++;
            panel_queue_warn_rate_limited();
            return true;
        }
    }

    switch (ev->type) {
        case PANEL_EV_BUTTON: s_button_drop_count++; break;
        case PANEL_EV_JOG:    s_jog_drop_count++;    break;
        case PANEL_EV_BROWSE: s_browse_drop_count++; break;
        case PANEL_EV_PITCH:  s_pitch_drop_count++;  break;
        default: break;
    }
    panel_queue_warn_rate_limited();
    return false;
}

static void panel_queue_pitch(uint16_t pitch)
{
    panel_event_t ev = { .type = PANEL_EV_PITCH, .id = 0, .value = (int16_t)pitch };
    if (!panel_queue_send(&ev)) {
        s_pending_pitch = pitch;
        s_pending_pitch_valid = true;
    }
}

static void panel_flush_pending_pitch(void)
{
    if (!s_pending_pitch_valid || uxQueueSpacesAvailable(s_queue) == 0) {
        return;
    }
    uint16_t pitch = s_pending_pitch;
    s_pending_pitch_valid = false;
    panel_queue_pitch(pitch);
}

// ─── Scan task ────────────────────────────────────────────────────────────────

static void scan_task(void *arg)
{
    TickType_t last_wake = xTaskGetTickCount();
    uint32_t   pitch_div = 0;   // read pitch every other tick (10 ms)

    while (1) {
        panel_flush_pending_pitch();

        // Buttons
        for (int i = 0; i < BTN_COUNT; i++) {
            bool raw = (gpio_get_level(BUTTON_PINS[i]) == 0);  // active-low

            if (raw == s_btn[i].confirmed) {
                s_btn[i].stable_cnt = 0;
                continue;
            }

            s_btn[i].stable_cnt++;
            if (s_btn[i].stable_cnt >= debounce_ticks_for((button_id_t)i)) {
                s_btn[i].confirmed  = raw;
                s_btn[i].stable_cnt = 0;

                panel_event_t ev = {
                    .type  = PANEL_EV_BUTTON,
                    .id    = (uint8_t)i,
                    .value = raw ? 1 : 0,
                };
                panel_queue_send(&ev);
            }
        }

        // Encoders
        int16_t jog;
        int16_t browse;
        panel_encoder_read_deltas(&jog, &browse);

        if (jog != 0) {
            panel_event_t ev = { .type = PANEL_EV_JOG, .id = 0, .value = jog };
            panel_queue_send(&ev);
        }
        if (browse != 0) {
            panel_event_t ev = { .type = PANEL_EV_BROWSE, .id = 1, .value = browse };
            panel_queue_send(&ev);
        }

        // Pitch (read every 10 ms to avoid flooding)
        if (++pitch_div >= 2) {
            pitch_div = 0;
            uint16_t pitch = panel_pitch_read_14bit();
            if (pitch != UINT16_MAX) {
                panel_queue_pitch(pitch);
            }
        }

        // LED blink tick
        panel_led_tick(SCAN_PERIOD_MS);

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(SCAN_PERIOD_MS));
    }
}

// ─── Init ─────────────────────────────────────────────────────────────────────

esp_err_t panel_io_init(QueueHandle_t *event_queue_out)
{
    memset(s_btn, 0, sizeof(s_btn));

    s_queue = xQueueCreate(32, sizeof(panel_event_t));
    if (!s_queue) return ESP_ERR_NO_MEM;

    // Configure button GPIOs
    for (int i = 0; i < BTN_COUNT; i++) {
        gpio_config_t cfg = {
            .mode         = GPIO_MODE_INPUT,
            .pull_up_en   = GPIO_PULLUP_ENABLE,   // active-low with internal pull-up
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,
            .pin_bit_mask = 1ULL << BUTTON_PINS[i],
        };
        ESP_RETURN_ON_ERROR(gpio_config(&cfg), TAG, "gpio btn %d", i);

        // Initialise confirmed state from the current GPIO level so we don't
        // generate a spurious event at startup.
        s_btn[i].confirmed = (gpio_get_level(BUTTON_PINS[i]) == 0);
    }

    ESP_RETURN_ON_ERROR(panel_encoder_init(), TAG, "encoder init");
    ESP_RETURN_ON_ERROR(panel_pitch_init(), TAG, "pitch init");
    ESP_RETURN_ON_ERROR(panel_leds_init(), TAG, "led init");

    if (xTaskCreate(scan_task, "panel_scan", 3072, NULL, 6, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    *event_queue_out = s_queue;
    ESP_LOGI(TAG, "ready, %d buttons, encoders, ADC pitch, %d LEDs", BTN_COUNT, LED_COUNT);
    return ESP_OK;
}
