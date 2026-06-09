#include "audio_mixer.h"
#include <assert.h>
#include <stdio.h>

static void test_fader_gain_clamps_to_unit_range(void)
{
    assert(audio_mixer_fader_gain(0) == 0.0f);
    assert(audio_mixer_fader_gain(AUDIO_MIXER_CONTROL_MAX) == 1.0f);
    assert(audio_mixer_fader_gain(AUDIO_MIXER_CONTROL_CENTER) > 0.49f);
    assert(audio_mixer_fader_gain(AUDIO_MIXER_CONTROL_CENTER) < 0.51f);
}

static void test_crossfader_keeps_center_both_decks_open(void)
{
    float deck1 = 0.0f;
    float deck2 = 0.0f;

    audio_mixer_crossfader_gains(0, &deck1, &deck2);
    assert(deck1 == 1.0f);
    assert(deck2 == 0.0f);

    audio_mixer_crossfader_gains(AUDIO_MIXER_CONTROL_CENTER, &deck1, &deck2);
    assert(deck1 == 1.0f);
    assert(deck2 == 1.0f);

    audio_mixer_crossfader_gains(AUDIO_MIXER_CONTROL_MAX, &deck1, &deck2);
    assert(deck1 == 0.0f);
    assert(deck2 == 1.0f);
}

static void test_mixer_saturates_instead_of_wrapping(void)
{
    assert(audio_mixer_mix_sample(20000, 20000, 1.0f, 1.0f) == 32767);
    assert(audio_mixer_mix_sample(-20000, -20000, 1.0f, 1.0f) == -32768);
    assert(audio_mixer_mix_sample(10000, -4000, 0.5f, 0.25f) == 4000);
}

static void test_stereo_frame_uses_channel_and_crossfader_gains(void)
{
    audio_mixer_frame_t out = audio_mixer_mix_stereo(
        (audio_mixer_frame_t){ .left = 10000, .right = -10000 },
        (audio_mixer_frame_t){ .left = 10000, .right = 10000 },
        1.0f,
        1.0f,
        AUDIO_MIXER_CONTROL_CENTER);

    assert(out.left == 20000);
    assert(out.right == 0);

    out = audio_mixer_mix_stereo(
        (audio_mixer_frame_t){ .left = 12000, .right = 12000 },
        (audio_mixer_frame_t){ .left = 30000, .right = 30000 },
        1.0f,
        1.0f,
        0);

    assert(out.left == 12000);
    assert(out.right == 12000);
}

int main(void)
{
    test_fader_gain_clamps_to_unit_range();
    test_crossfader_keeps_center_both_decks_open();
    test_mixer_saturates_instead_of_wrapping();
    test_stereo_frame_uses_channel_and_crossfader_gains();
    puts("audio_mixer tests passed");
    return 0;
}
