#include "monitor_pcm_link.h"

#include <string.h>

static monitor_pcm_link_stats_t s_stats;
static bool s_enabled;

esp_err_t monitor_pcm_link_init(void)
{
    memset(&s_stats, 0, sizeof(s_stats));
    s_enabled = false;
    return ESP_OK;
}

esp_err_t monitor_pcm_link_set_format(uint32_t sample_rate,
                                      uint8_t channels,
                                      uint8_t bits_per_sample)
{
    if (sample_rate == 0u || channels == 0u || bits_per_sample == 0u) {
        return ESP_ERR_INVALID_ARG;
    }

    s_stats.sample_rate = sample_rate;
    s_stats.channels = channels;
    s_stats.bits_per_sample = bits_per_sample;
    return ESP_OK;
}

bool monitor_pcm_link_write_nonblocking(const int16_t *interleaved_stereo, size_t frames)
{
    if (!interleaved_stereo || frames == 0u || !s_enabled) {
        s_stats.dropped_blocks++;
        return false;
    }

    s_stats.submitted_blocks++;
    s_stats.submitted_frames += (uint32_t)frames;
    return true;
}

void monitor_pcm_link_get_stats(monitor_pcm_link_stats_t *out)
{
    if (out) {
        *out = s_stats;
    }
}
