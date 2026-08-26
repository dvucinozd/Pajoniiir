/* SPDX-License-Identifier: Apache-2.0 */
#include "controller_audio_resampler.h"

#include <string.h>

bool controller_audio_resampler_init(controller_audio_resampler_t *resampler,
                                     uint32_t source_rate,
                                     uint32_t target_rate,
                                     uint8_t channels)
{
    if (!resampler || source_rate == 0u || target_rate == 0u ||
        channels == 0u || channels > 8u) {
        return false;
    }
    memset(resampler, 0, sizeof(*resampler));
    resampler->source_rate = source_rate;
    resampler->target_rate = target_rate;
    resampler->channels = channels;
    return true;
}

size_t controller_audio_resampler_output_bound(uint32_t source_rate,
                                               uint32_t target_rate,
                                               size_t input_frames)
{
    if (source_rate == 0u || target_rate == 0u || input_frames == 0u) {
        return 0u;
    }
    const uint64_t scaled = (uint64_t)input_frames * target_rate;
    const uint64_t bound =
        (scaled + (uint64_t)source_rate - 1u) / source_rate + 1u;
    return bound > SIZE_MAX ? SIZE_MAX : (size_t)bound;
}

static int16_t resample_linear(int16_t previous,
                               int16_t current,
                               uint64_t offset,
                               uint32_t interval)
{
    const int64_t mixed =
        (int64_t)previous * (int64_t)((uint64_t)interval - offset) +
        (int64_t)current * (int64_t)offset;
    const int64_t rounding = (int64_t)interval / 2;
    return (int16_t)((mixed >= 0 ? mixed + rounding : mixed - rounding) /
                     (int64_t)interval);
}

size_t controller_audio_resampler_process(controller_audio_resampler_t *resampler,
                                          const int16_t *input,
                                          size_t input_frames,
                                          int16_t *output,
                                          size_t output_capacity_frames)
{
    if (!resampler || !input || !output || input_frames == 0u ||
        resampler->source_rate == 0u || resampler->target_rate == 0u ||
        resampler->channels == 0u || resampler->channels > 8u) {
        return 0u;
    }
    const size_t bound = controller_audio_resampler_output_bound(
        resampler->source_rate, resampler->target_rate, input_frames);
    if (bound == 0u || output_capacity_frames < bound) {
        return 0u;
    }

    size_t produced = 0u;
    size_t input_index = 0u;
    if (!resampler->has_previous) {
        memcpy(output, input,
               (size_t)resampler->channels * sizeof(*output));
        memcpy(resampler->previous, input,
               (size_t)resampler->channels * sizeof(*input));
        produced = 1u;
        input_index = 1u;
        resampler->input_frames_seen = 1u;
        resampler->next_output_time = resampler->source_rate;
        resampler->has_previous = true;
    }

    for (; input_index < input_frames; ++input_index) {
        const int16_t *current =
            &input[input_index * (size_t)resampler->channels];
        const uint64_t current_index = resampler->input_frames_seen;
        const uint64_t previous_time =
            (current_index - 1u) * resampler->target_rate;
        const uint64_t current_time =
            current_index * resampler->target_rate;

        while (resampler->next_output_time <= current_time) {
            const uint64_t offset =
                resampler->next_output_time - previous_time;
            int16_t *target =
                &output[produced * (size_t)resampler->channels];
            for (uint8_t channel = 0u;
                 channel < resampler->channels;
                 ++channel) {
                target[channel] = resample_linear(
                    resampler->previous[channel], current[channel], offset,
                    resampler->target_rate);
            }
            produced++;
            resampler->next_output_time += resampler->source_rate;
        }

        memcpy(resampler->previous, current,
               (size_t)resampler->channels * sizeof(*current));
        resampler->input_frames_seen++;
    }
    return produced;
}
