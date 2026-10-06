#include "audio_resampler.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

typedef struct { unsigned position; } source_t;
static bool read_frame(void *ctx, audio_mixer_frame_t *out)
{
    source_t *s = ctx;
    int16_t v = (int16_t)(12000.0 * sin(6.283185307179586 * 30000.0 * s->position++ / 96000.0));
    *out = (audio_mixer_frame_t){v, v};
    return true;
}
int main(void)
{
    static audio_resampler_state_t filtered, reference;
    source_t a = {0}, b = {0};
    audio_resampler_reset(&filtered);
    audio_resampler_reset(&reference);
    filtered.antialias = true;
    double energy = 0, unfiltered_energy = 0;
    for (unsigned i = 0; i < 10000; ++i) {
        uint32_t ca, cb;
        audio_mixer_frame_t fa = audio_resampler_next(&filtered, 2.0f, read_frame, &a, &ca);
        audio_mixer_frame_t fb = audio_resampler_next(&reference, 2.0f, read_frame, &b, &cb);
        assert(ca == cb && a.position == b.position);
        if (i > 1000) {
            energy += (double)fa.left * fa.left;
            unfiltered_energy += (double)fb.left * fb.left;
        }
    }
    assert(sqrt(energy / 8999) < 85);
    assert(sqrt(unfiltered_energy / 8999) > 8000);
    puts("PASS antialias: suppressed 30 kHz alias, unchanged source clock");
    return 0;
}
