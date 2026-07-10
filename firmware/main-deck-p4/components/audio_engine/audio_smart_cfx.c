#include "audio_smart_cfx.h"

#include "audio_filter.h"

/* Smart CFX response curve: smoothstep (3x^2 - 2x^3) on the knob's distance
 * from the detent. Compared to the raw knob it stays gentle right around the
 * detent (fine control before the effect bites), passes through 1:1 at half
 * turn, and flattens again near the ends for precise full-kill riding —
 * instead of the old x^2 curve that deadened the whole first half. */
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

    /* smoothstep: y = mag^2 * (3*max - 2*mag) / max^2, rounded. */
    uint64_t num = (uint64_t)mag * (uint64_t)mag *
                   (uint64_t)(3u * max - 2u * mag);
    uint64_t den = (uint64_t)max * (uint64_t)max;
    uint32_t curved = (uint32_t)((num + den / 2u) / den);
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
