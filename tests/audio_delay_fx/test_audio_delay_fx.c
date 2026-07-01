#include "audio_delay_fx.h"
#include "audio_mixer.h"

#include <assert.h>
#include <stdio.h>

static void test_disabled_bypasses_input(void)
{
    int16_t left[16] = { 0 };
    int16_t right[16] = { 0 };
    audio_delay_fx_t fx;
    audio_delay_fx_init(&fx, left, right, 16u, 1000u);

    audio_delay_fx_config_t cfg = {
        .enabled = false,
        .delay_ms = 4,
        .wet_q15 = 16384,
        .feedback_q15 = 8192,
    };
    audio_delay_fx_configure(&fx, &cfg);

    audio_mixer_frame_t in = { .left = 1234, .right = -2345 };
    audio_mixer_frame_t out = audio_delay_fx_process_frame(&fx, in);
    assert(out.left == in.left);
    assert(out.right == in.right);
}

static void test_impulse_reappears_after_delay(void)
{
    int16_t left[16] = { 0 };
    int16_t right[16] = { 0 };
    audio_delay_fx_t fx;
    audio_delay_fx_init(&fx, left, right, 16u, 1000u);

    audio_delay_fx_config_t cfg = {
        .enabled = true,
        .delay_ms = 4,
        .wet_q15 = 16384,
        .feedback_q15 = 0,
    };
    audio_delay_fx_configure(&fx, &cfg);

    audio_mixer_frame_t out0 = audio_delay_fx_process_frame(
        &fx,
        (audio_mixer_frame_t) { .left = 10000, .right = 10000 });
    assert(out0.left == 10000);
    assert(out0.right == 10000);

    for (int i = 0; i < 3; i++) {
        audio_mixer_frame_t out = audio_delay_fx_process_frame(&fx, (audio_mixer_frame_t) { 0 });
        assert(out.left == 0);
        assert(out.right == 0);
    }

    audio_mixer_frame_t delayed = audio_delay_fx_process_frame(&fx, (audio_mixer_frame_t) { 0 });
    assert(delayed.left > 4500 && delayed.left < 5500);
    assert(delayed.right > 4500 && delayed.right < 5500);
}

static void test_feedback_decays_and_reset_clears_tail(void)
{
    int16_t left[32] = { 0 };
    int16_t right[32] = { 0 };
    audio_delay_fx_t fx;
    audio_delay_fx_init(&fx, left, right, 32u, 1000u);

    audio_delay_fx_config_t cfg = {
        .enabled = true,
        .delay_ms = 2,
        .wet_q15 = 16384,
        .feedback_q15 = 8192,
    };
    audio_delay_fx_configure(&fx, &cfg);

    (void)audio_delay_fx_process_frame(
        &fx,
        (audio_mixer_frame_t) { .left = 12000, .right = 12000 });
    (void)audio_delay_fx_process_frame(&fx, (audio_mixer_frame_t) { 0 });
    audio_mixer_frame_t first_echo = audio_delay_fx_process_frame(&fx, (audio_mixer_frame_t) { 0 });
    (void)audio_delay_fx_process_frame(&fx, (audio_mixer_frame_t) { 0 });
    audio_mixer_frame_t second_echo = audio_delay_fx_process_frame(&fx, (audio_mixer_frame_t) { 0 });

    assert(first_echo.left > second_echo.left);
    assert(second_echo.left > 0);

    audio_delay_fx_reset(&fx);
    for (int i = 0; i < 8; i++) {
        audio_mixer_frame_t out = audio_delay_fx_process_frame(&fx, (audio_mixer_frame_t) { 0 });
        assert(out.left == 0);
        assert(out.right == 0);
    }
}

static void test_null_or_zero_buffer_bypasses(void)
{
    audio_delay_fx_t fx;
    audio_delay_fx_init(&fx, NULL, NULL, 0u, 1000u);

    audio_delay_fx_config_t cfg = {
        .enabled = true,
        .delay_ms = 10,
        .wet_q15 = 32767,
        .feedback_q15 = 32767,
    };
    audio_delay_fx_configure(&fx, &cfg);

    audio_mixer_frame_t in = { .left = -3000, .right = 3000 };
    audio_mixer_frame_t out = audio_delay_fx_process_frame(&fx, in);
    assert(out.left == in.left);
    assert(out.right == in.right);
}

int main(void)
{
    test_disabled_bypasses_input();
    test_impulse_reappears_after_delay();
    test_feedback_decays_and_reset_clears_tail();
    test_null_or_zero_buffer_bypasses();
    puts("audio_delay_fx tests passed");
    return 0;
}
