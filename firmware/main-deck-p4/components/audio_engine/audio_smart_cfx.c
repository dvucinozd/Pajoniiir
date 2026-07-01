#include "audio_smart_cfx.h"

#include "audio_filter.h"

uint16_t audio_smart_cfx_curve_raw(uint16_t raw)
{
    if (raw > AUDIO_FILTER_RAW_MAX) {
        raw = AUDIO_FILTER_RAW_MAX;
    }
    if (raw == AUDIO_FILTER_RAW_CENTER ||
        raw == AUDIO_FILTER_RAW_MIN ||
        raw == AUDIO_FILTER_RAW_MAX) {
        return raw;
    }

    int32_t delta = (int32_t)raw - (int32_t)AUDIO_FILTER_RAW_CENTER;
    int32_t sign = delta < 0 ? -1 : 1;
    uint32_t mag = (uint32_t)(delta < 0 ? -delta : delta);
    uint32_t max = AUDIO_FILTER_RAW_CENTER;
    if (mag > max) {
        mag = max;
    }

    uint32_t curved = (mag * mag + (max / 2u)) / max;
    uint32_t min_audible = max / 24u;
    if (mag > min_audible && curved < min_audible) {
        curved = min_audible;
    }

    int32_t out = (int32_t)AUDIO_FILTER_RAW_CENTER + (sign * (int32_t)curved);
    if (out < (int32_t)AUDIO_FILTER_RAW_MIN) {
        out = AUDIO_FILTER_RAW_MIN;
    }
    if (out > (int32_t)AUDIO_FILTER_RAW_MAX) {
        out = AUDIO_FILTER_RAW_MAX;
    }
    return (uint16_t)out;
}
