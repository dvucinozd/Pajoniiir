#include "panel_io_priv.h"
#include "esp_check.h"
#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "encoder";

// ─── Pin assignment ───────────────────────────────────────────────────────────
#define PIN_JOG_A GPIO_NUM_15
#define PIN_JOG_B GPIO_NUM_16
#define PIN_BROWSE_A GPIO_NUM_17
#define PIN_BROWSE_B GPIO_NUM_18

// PCNT unit handle
static pcnt_unit_handle_t s_jog_unit;
static pcnt_unit_handle_t s_browse_unit;

// Shadow counter; used to compute delta between polls.
static int s_jog_last = 0;
static int s_browse_last = 0;

static esp_err_t configure_encoder_gpio(gpio_num_t pin)
{
    gpio_config_t cfg = {
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
        .pin_bit_mask = 1ULL << pin,
    };
    return gpio_config(&cfg);
}

static esp_err_t init_encoder_unit(gpio_num_t pin_a, gpio_num_t pin_b,
                                   pcnt_unit_handle_t *out_unit)
{
    ESP_RETURN_ON_ERROR(configure_encoder_gpio(pin_a), TAG, "gpio a");
    ESP_RETURN_ON_ERROR(configure_encoder_gpio(pin_b), TAG, "gpio b");

    pcnt_unit_config_t unit_cfg = {
        .low_limit  = INT16_MIN,
        .high_limit = INT16_MAX,
        .flags.accum_count = true,
    };
    ESP_RETURN_ON_ERROR(pcnt_new_unit(&unit_cfg, out_unit), TAG, "new unit");

    // 1 µs glitch filter removes debounce spikes without affecting signal quality.
    pcnt_glitch_filter_config_t filter = { .max_glitch_ns = 1000 };
    ESP_RETURN_ON_ERROR(pcnt_unit_set_glitch_filter(*out_unit, &filter), TAG, "filter");

    // Channel A: edge on A, direction from B → X4 quadrature.
    pcnt_channel_handle_t chan_a, chan_b;
    pcnt_chan_config_t chan_a_cfg = {
        .edge_gpio_num  = pin_a,
        .level_gpio_num = pin_b,
    };
    ESP_RETURN_ON_ERROR(pcnt_new_channel(*out_unit, &chan_a_cfg, &chan_a), TAG, "ch_a");
    ESP_RETURN_ON_ERROR(pcnt_channel_set_edge_action(chan_a,
        PCNT_CHANNEL_EDGE_ACTION_DECREASE,
        PCNT_CHANNEL_EDGE_ACTION_INCREASE), TAG, "ch_a edge");
    ESP_RETURN_ON_ERROR(pcnt_channel_set_level_action(chan_a,
        PCNT_CHANNEL_LEVEL_ACTION_KEEP,
        PCNT_CHANNEL_LEVEL_ACTION_INVERSE), TAG, "ch_a level");

    // Channel B: edge on B, direction from A.
    pcnt_chan_config_t chan_b_cfg = {
        .edge_gpio_num  = pin_b,
        .level_gpio_num = pin_a,
    };
    ESP_RETURN_ON_ERROR(pcnt_new_channel(*out_unit, &chan_b_cfg, &chan_b), TAG, "ch_b");
    ESP_RETURN_ON_ERROR(pcnt_channel_set_edge_action(chan_b,
        PCNT_CHANNEL_EDGE_ACTION_INCREASE,
        PCNT_CHANNEL_EDGE_ACTION_DECREASE), TAG, "ch_b edge");
    ESP_RETURN_ON_ERROR(pcnt_channel_set_level_action(chan_b,
        PCNT_CHANNEL_LEVEL_ACTION_KEEP,
        PCNT_CHANNEL_LEVEL_ACTION_INVERSE), TAG, "ch_b level");

    ESP_RETURN_ON_ERROR(pcnt_unit_enable(*out_unit), TAG, "enable");
    ESP_RETURN_ON_ERROR(pcnt_unit_clear_count(*out_unit), TAG, "clear");
    ESP_RETURN_ON_ERROR(pcnt_unit_start(*out_unit), TAG, "start");

    return ESP_OK;
}

esp_err_t panel_encoder_init(void)
{
    ESP_RETURN_ON_ERROR(init_encoder_unit(PIN_JOG_A, PIN_JOG_B, &s_jog_unit), TAG, "jog init");
    ESP_RETURN_ON_ERROR(init_encoder_unit(PIN_BROWSE_A, PIN_BROWSE_B, &s_browse_unit), TAG, "browse init");
    ESP_LOGI(TAG, "jog A=%d B=%d, browse A=%d B=%d",
             PIN_JOG_A, PIN_JOG_B, PIN_BROWSE_A, PIN_BROWSE_B);
    return ESP_OK;
}

void panel_encoder_read_deltas(int16_t *jog_delta, int16_t *browse_delta)
{
    int jog_count;
    int browse_count;
    pcnt_unit_get_count(s_jog_unit, &jog_count);
    pcnt_unit_get_count(s_browse_unit, &browse_count);
    *jog_delta = (int16_t)(jog_count - s_jog_last);
    *browse_delta = (int16_t)(browse_count - s_browse_last);
    s_jog_last = jog_count;
    s_browse_last = browse_count;
}
