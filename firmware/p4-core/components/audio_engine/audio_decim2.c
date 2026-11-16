#include "audio_decim2.h"
#include "audio_decim2_table.h"

#include <string.h>

#define DECIM2_MID (2u * AUDIO_DECIM2_ODD_TAPS - 1u)   /* centre: 31 frames back */

void audio_decim2_reset(audio_decim2_t *d)
{
    if (d) {
        memset(d, 0, sizeof(*d));
    }
}

static int16_t decim2_round(float v)
{
    if (v >= 32767.0f) return 32767;
    if (v <= -32768.0f) return -32768;
    return (int16_t)(v >= 0.0f ? v + 0.5f : v - 0.5f);
}

size_t audio_decim2_process(audio_decim2_t *d, const int16_t *in, size_t n, int16_t *out)
{
    if (!d || !in || !out) {
        return 0u;
    }
    const uint32_t mask = AUDIO_DECIM2_HIST - 1u;
    size_t written = 0u;
    for (size_t i = 0; i < n; i++) {
        d->hist[d->pos][0] = (float)in[i * 2u];
        d->hist[d->pos][1] = (float)in[i * 2u + 1u];
        const uint32_t newest = d->pos;
        d->pos = (d->pos + 1u) & mask;
        d->odd = !d->odd;
        if (d->odd) {
            continue;   /* one output per pair */
        }
        const uint32_t centre = (newest - DECIM2_MID) & mask;
        float l = AUDIO_DECIM2_CENTRE * d->hist[centre][0];
        float r = AUDIO_DECIM2_CENTRE * d->hist[centre][1];
        for (uint32_t k = 0; k < AUDIO_DECIM2_ODD_TAPS; k++) {
            const uint32_t off = 2u * k + 1u;
            const uint32_t a = (centre - off) & mask;
            const uint32_t b = (centre + off) & mask;
            l += s_decim2_odd[k] * (d->hist[a][0] + d->hist[b][0]);
            r += s_decim2_odd[k] * (d->hist[a][1] + d->hist[b][1]);
        }
        out[written * 2u] = decim2_round(l);
        out[written * 2u + 1u] = decim2_round(r);
        written++;
    }
    return written;
}

int audio_decim2_stages(uint32_t source_rate)
{
    if (source_rate <= 64000u) return 0;
    if (source_rate <= 128000u) return 1;
    if (source_rate <= 256000u) return 2;
    return -1;
}
