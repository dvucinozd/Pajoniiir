#include "panel_io_priv.h"
#include "esp_check.h"
#include "calibration.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include <stdint.h>

static const char *TAG = "pitch";

// GPIO1 = ADC1 channel 0. ADC1 is preferred over ADC2 to avoid any
// potential conflict with Wi-Fi (unused here, but good practice).
#define PITCH_ADC_UNIT    ADC_UNIT_1
#define PITCH_ADC_CHANNEL ADC_CHANNEL_0   // GPIO1

// Minimum raw change (14-bit) required to emit a new pitch event.
// Reduces noise-driven event floods when the fader is stationary.
#define PITCH_CHANGE_THRESHOLD 8

static adc_oneshot_unit_handle_t s_adc;
static uint16_t s_last_cooked = UINT16_MAX;

esp_err_t panel_pitch_init(void)
{
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id  = PITCH_ADC_UNIT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&init_cfg, &s_adc), TAG, "adc unit");

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten    = ADC_ATTEN_DB_12,   // 0–3.3 V range
        .bitwidth = ADC_BITWIDTH_12,   // 0–4095 raw
    };
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(s_adc, PITCH_ADC_CHANNEL, &chan_cfg), TAG, "adc channel");

    ESP_LOGI(TAG, "ADC1 ch%d (GPIO1) ready", PITCH_ADC_CHANNEL);
    return ESP_OK;
}

uint16_t panel_pitch_read_14bit(void)
{
    int raw = 0;
    if (adc_oneshot_read(s_adc, PITCH_ADC_CHANNEL, &raw) != ESP_OK) {
        return UINT16_MAX;
    }

    // Scale 12-bit raw (0–4095) to 14-bit (0–16383).
    uint16_t raw_14 = (uint16_t)(raw << 2);

    // Apply calibration (centre offset, deadzone, direction).
    uint16_t cooked = calibration_apply_pitch(raw_14);

    if (s_last_cooked == UINT16_MAX ||
        (cooked > s_last_cooked ? cooked - s_last_cooked : s_last_cooked - cooked) >= PITCH_CHANGE_THRESHOLD) {
        s_last_cooked = cooked;
        return cooked;
    }

    return UINT16_MAX;   // no significant change
}
