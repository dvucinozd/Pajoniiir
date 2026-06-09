#include "audio_mixer.h"

static float clamp_gain(float gain)
{
    if (gain < 0.0f) return 0.0f;
    if (gain > 1.0f) return 1.0f;
    return gain;
}

float audio_mixer_fader_gain(uint16_t raw)
{
    if (raw >= AUDIO_MIXER_CONTROL_MAX) return 1.0f;
    return (float)raw / (float)AUDIO_MIXER_CONTROL_MAX;
}

void audio_mixer_crossfader_gains(uint16_t raw, float *deck1_gain, float *deck2_gain)
{
    float d1 = 1.0f;
    float d2 = 1.0f;

    if (raw <= AUDIO_MIXER_CONTROL_CENTER) {
        d2 = (float)raw / (float)AUDIO_MIXER_CONTROL_CENTER;
    } else {
        d1 = (float)(AUDIO_MIXER_CONTROL_MAX - raw) /
             (float)(AUDIO_MIXER_CONTROL_MAX - AUDIO_MIXER_CONTROL_CENTER);
    }

    if (deck1_gain) *deck1_gain = clamp_gain(d1);
    if (deck2_gain) *deck2_gain = clamp_gain(d2);
}

int16_t audio_mixer_mix_sample(int16_t deck1,
                               int16_t deck2,
                               float deck1_gain,
                               float deck2_gain)
{
    float mixed = ((float)deck1 * clamp_gain(deck1_gain)) +
                  ((float)deck2 * clamp_gain(deck2_gain));

    if (mixed > 32767.0f) return 32767;
    if (mixed < -32768.0f) return -32768;
    return (int16_t)(mixed >= 0.0f ? mixed + 0.5f : mixed - 0.5f);
}

audio_mixer_frame_t audio_mixer_apply_gain(audio_mixer_frame_t frame, float gain)
{
    return (audio_mixer_frame_t) {
        .left = audio_mixer_mix_sample(frame.left, 0, gain, 0.0f),
        .right = audio_mixer_mix_sample(frame.right, 0, gain, 0.0f),
    };
}

audio_mixer_frame_t audio_mixer_mix_stereo(audio_mixer_frame_t deck1,
                                           audio_mixer_frame_t deck2,
                                           float deck1_channel_gain,
                                           float deck2_channel_gain,
                                           uint16_t crossfader)
{
    float xf1 = 1.0f;
    float xf2 = 1.0f;
    audio_mixer_crossfader_gains(crossfader, &xf1, &xf2);

    float g1 = clamp_gain(deck1_channel_gain) * xf1;
    float g2 = clamp_gain(deck2_channel_gain) * xf2;

    return (audio_mixer_frame_t) {
        .left = audio_mixer_mix_sample(deck1.left, deck2.left, g1, g2),
        .right = audio_mixer_mix_sample(deck1.right, deck2.right, g1, g2),
    };
}
