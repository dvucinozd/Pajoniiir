#include <assert.h>
#include <stdio.h>

#include "audio_pad_fx.h"

static void test_pad_fx_defaults_to_bypass(void)
{
    audio_pad_fx_state_t fx;
    audio_pad_fx_init(&fx, 44100u);

    audio_mixer_frame_t in = { .left = 1200, .right = -1200 };
    audio_mixer_frame_t out = audio_pad_fx_process_frame(&fx, in);

    assert(out.left == in.left);
    assert(out.right == in.right);
    assert(!audio_pad_fx_is_active(&fx));
}

static void test_pad_fx_filter_pad_changes_signal(void)
{
    audio_pad_fx_state_t fx;
    audio_pad_fx_init(&fx, 44100u);
    audio_pad_fx_set(&fx, (audio_pad_fx_config_t) {
        .mode = AUDIO_PAD_FX_MODE_PAD_FX1,
        .pad = 0,
        .active = true,
    });

    audio_mixer_frame_t in = { .left = 16000, .right = -16000 };
    audio_mixer_frame_t out = audio_pad_fx_process_frame(&fx, in);

    assert(audio_pad_fx_is_active(&fx));
    assert(out.left != in.left || out.right != in.right);
}

static void test_pad_fx_release_clears_active_pad(void)
{
    audio_pad_fx_state_t fx;
    audio_pad_fx_init(&fx, 44100u);
    audio_pad_fx_set(&fx, (audio_pad_fx_config_t) {
        .mode = AUDIO_PAD_FX_MODE_PAD_FX2,
        .pad = 3,
        .active = true,
    });
    assert(audio_pad_fx_is_active(&fx));

    audio_pad_fx_set(&fx, (audio_pad_fx_config_t) {
        .mode = AUDIO_PAD_FX_MODE_PAD_FX2,
        .pad = 3,
        .active = false,
    });
    assert(!audio_pad_fx_is_active(&fx));
}

int main(void)
{
    test_pad_fx_defaults_to_bypass();
    test_pad_fx_filter_pad_changes_signal();
    test_pad_fx_release_clears_active_pad();
    puts("audio_pad_fx tests passed");
    return 0;
}
