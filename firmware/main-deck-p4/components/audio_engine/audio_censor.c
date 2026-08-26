#include "audio_censor.h"

#include <stddef.h>
#include <string.h>

#define AUDIO_CENSOR_Q32_SCALE 4294967296.0f
#define AUDIO_CENSOR_MAX_STEP  8.0f

static uint64_t censor_step_q32(uint32_t source_sample_rate,
                                uint32_t output_sample_rate,
                                float speed_factor)
{
    if (source_sample_rate == 0u || output_sample_rate == 0u) return 0u;
    if (!(speed_factor > 0.0f)) speed_factor = 1.0f;
    float step = ((float)source_sample_rate / (float)output_sample_rate) *
                 speed_factor;
    if (step > AUDIO_CENSOR_MAX_STEP) step = AUDIO_CENSOR_MAX_STEP;
    if (!(step > 0.0f)) return 0u;
    return (uint64_t)(step * AUDIO_CENSOR_Q32_SCALE + 0.5f);
}

static int16_t interpolate_sample(int16_t newer, int16_t older, uint32_t frac)
{
    int64_t delta = (int64_t)older - (int64_t)newer;
    int64_t value = (int64_t)newer +
                    ((delta * (int64_t)frac) / (int64_t)UINT64_C(0x100000000));
    if (value > INT16_MAX) value = INT16_MAX;
    if (value < INT16_MIN) value = INT16_MIN;
    return (int16_t)value;
}

static int16_t scale_sample(int16_t sample, uint32_t numerator, uint32_t denominator)
{
    if (denominator == 0u || numerator == 0u) return 0;
    return (int16_t)(((int32_t)sample * (int32_t)numerator) /
                     (int32_t)denominator);
}

void audio_censor_init(audio_censor_t *censor)
{
    if (!censor) return;
    memset(censor, 0, sizeof(*censor));
}

void audio_censor_reset(audio_censor_t *censor)
{
    audio_censor_init(censor);
}

bool audio_censor_begin(audio_censor_t *censor,
                        uint64_t origin_seq,
                        uint32_t source_sample_rate,
                        uint32_t output_sample_rate,
                        float speed_factor,
                        uint32_t release_frames)
{
    if (!censor || release_frames == 0u) return false;
    uint64_t step = censor_step_q32(source_sample_rate, output_sample_rate,
                                    speed_factor);
    if (step == 0u) return false;
    audio_censor_init(censor);
    censor->active = true;
    censor->origin_seq = origin_seq;
    censor->step_q32 = step;
    censor->release_frames = release_frames;
    return true;
}

void audio_censor_set_rate(audio_censor_t *censor,
                           uint32_t source_sample_rate,
                           uint32_t output_sample_rate,
                           float speed_factor)
{
    if (!censor || !censor->active) return;
    uint64_t step = censor_step_q32(source_sample_rate, output_sample_rate,
                                    speed_factor);
    if (step != 0u) censor->step_q32 = step;
}

void audio_censor_release(audio_censor_t *censor)
{
    if (!censor || !censor->active || censor->releasing) return;
    censor->releasing = true;
    censor->release_remaining = censor->release_frames;
}

bool audio_censor_is_active(const audio_censor_t *censor)
{
    return censor && censor->active;
}

static bool read_reverse(audio_censor_t *censor,
                         audio_censor_read_fn read_frame,
                         void *read_ctx,
                         audio_mixer_frame_t *out)
{
    uint64_t whole = censor->distance_q32 >> 32;
    uint32_t frac = (uint32_t)censor->distance_q32;
    if (whole > censor->origin_seq) return false;
    uint64_t newer_seq = censor->origin_seq - whole;
    audio_mixer_frame_t newer = {0};
    if (!read_frame(read_ctx, newer_seq, &newer)) return false;

    *out = newer;
    if (frac != 0u && newer_seq > 0u) {
        audio_mixer_frame_t older = {0};
        if (read_frame(read_ctx, newer_seq - 1u, &older)) {
            out->left = interpolate_sample(newer.left, older.left, frac);
            out->right = interpolate_sample(newer.right, older.right, frac);
        }
    }
    return true;
}

bool audio_censor_render(audio_censor_t *censor,
                         audio_censor_read_fn read_frame,
                         void *read_ctx,
                         audio_mixer_frame_t *out_reverse,
                         float *out_reverse_gain)
{
    if (out_reverse) *out_reverse = (audio_mixer_frame_t){0};
    if (out_reverse_gain) *out_reverse_gain = 0.0f;
    if (!censor || !censor->active || !read_frame || !out_reverse ||
        !out_reverse_gain) {
        return false;
    }

    float gain = 1.0f;
    if (censor->releasing) {
        if (censor->release_remaining == 0u) {
            censor->active = false;
            censor->releasing = false;
            return false;
        }
        gain = (float)censor->release_remaining /
               (float)censor->release_frames;
    }

    audio_mixer_frame_t frame = {0};
    if (!censor->exhausted && read_reverse(censor, read_frame, read_ctx, &frame)) {
        censor->last_reverse = frame;
    } else {
        if (!censor->exhausted) {
            censor->exhausted = true;
            censor->edge_hits++;
            censor->edge_fade_remaining = AUDIO_CENSOR_EDGE_FADE_FRAMES;
        }
        if (censor->edge_fade_remaining > 0u) {
            frame.left = scale_sample(censor->last_reverse.left,
                                      censor->edge_fade_remaining,
                                      AUDIO_CENSOR_EDGE_FADE_FRAMES);
            frame.right = scale_sample(censor->last_reverse.right,
                                       censor->edge_fade_remaining,
                                       AUDIO_CENSOR_EDGE_FADE_FRAMES);
            censor->edge_fade_remaining--;
        }
    }

    *out_reverse = frame;
    *out_reverse_gain = gain;
    censor->distance_q32 += censor->step_q32;

    if (censor->releasing && censor->release_remaining > 0u) {
        censor->release_remaining--;
        if (censor->release_remaining == 0u) {
            censor->active = false;
            censor->releasing = false;
        }
    }
    return true;
}
