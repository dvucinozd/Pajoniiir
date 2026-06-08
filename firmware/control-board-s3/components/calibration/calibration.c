#include "calibration.h"
#include "esp_log.h"
#include <stdlib.h>

static const char *TAG = "calibration";

// Default calibration: ADC1 CH0 on 3.3V gives 12-bit raw → scale ×4 → 14-bit.
// Centre of a linear pot = mid-range = 8192. Deadzone ±200 counts ≈ ±1.2%.
// XDJ100SX Mixxx JS inverts the normalised pitch value, so invert=true here
// to match that behaviour when used with Mixxx in MIDI compat mode.
static calibration_t s_cal = {
    .center_14bit   = 8192,
    .deadzone_14bit = 200,
    .invert         = true,
};

esp_err_t calibration_init(void)
{
    ESP_LOGI(TAG, "pitch center=%u deadzone=%u invert=%d",
             s_cal.center_14bit, s_cal.deadzone_14bit, s_cal.invert);
    return ESP_OK;
}

uint16_t calibration_apply_pitch(uint16_t raw_14bit)
{
    int delta = (int)raw_14bit - (int)s_cal.center_14bit;

    if (abs(delta) <= (int)s_cal.deadzone_14bit) {
        return 8192;
    }

    if (s_cal.invert) {
        delta = -delta;
    }

    int result = delta + 8192;
    if (result < 0)     result = 0;
    if (result > 16383) result = 16383;

    return (uint16_t)result;
}

const calibration_t *calibration_get(void)
{
    return &s_cal;
}

void calibration_set(const calibration_t *cal)
{
    s_cal = *cal;
    ESP_LOGI(TAG, "updated: center=%u deadzone=%u invert=%d",
             s_cal.center_14bit, s_cal.deadzone_14bit, s_cal.invert);
}
