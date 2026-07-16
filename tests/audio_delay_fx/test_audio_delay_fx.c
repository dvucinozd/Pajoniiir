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
        .mode = AUDIO_DELAY_FX_MODE_ECHO,
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
        .mode = AUDIO_DELAY_FX_MODE_DELAY,
        .delay_ms = 4,
        .wet_q15 = 16384,
        /* DELAY must override a non-zero caller value to a one-shot tap. */
        .feedback_q15 = 24576,
    };
    audio_delay_fx_configure(&fx, &cfg);
    assert(fx.config.mode == AUDIO_DELAY_FX_MODE_DELAY);
    assert(fx.config.feedback_q15 == 0u);

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

    /* The same line position comes around again after another delay period.
     * A DELAY has no feedback, so there must not be a second repeat. */
    for (int i = 0; i < 4; i++) {
        audio_mixer_frame_t out = audio_delay_fx_process_frame(&fx, (audio_mixer_frame_t) { 0 });
        assert(out.left == 0);
        assert(out.right == 0);
    }
}

static void test_feedback_decays_and_reset_clears_tail(void)
{
    int16_t left[32] = { 0 };
    int16_t right[32] = { 0 };
    audio_delay_fx_t fx;
    audio_delay_fx_init(&fx, left, right, 32u, 1000u);

    audio_delay_fx_config_t cfg = {
        .enabled = true,
        .mode = AUDIO_DELAY_FX_MODE_ECHO,
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

static void test_switch_off_rings_tail_then_goes_silent(void)
{
    int16_t left[32] = { 0 };
    int16_t right[32] = { 0 };
    audio_delay_fx_t fx;
    audio_delay_fx_init(&fx, left, right, 32u, 1000u);

    audio_delay_fx_config_t cfg = {
        .enabled = true,
        .mode = AUDIO_DELAY_FX_MODE_ECHO,
        .delay_ms = 4,
        .wet_q15 = 16384,
        .feedback_q15 = 8192,
    };
    audio_delay_fx_configure(&fx, &cfg);

    (void)audio_delay_fx_process_frame(
        &fx,
        (audio_mixer_frame_t) { .left = 10000, .right = 10000 });
    for (int i = 0; i < 3; i++) {
        (void)audio_delay_fx_process_frame(&fx, (audio_mixer_frame_t) { 0 });
    }

    cfg.enabled = false;
    audio_delay_fx_configure(&fx, &cfg);
    assert(audio_delay_fx_is_ringing(&fx));

    audio_mixer_frame_t tail = audio_delay_fx_process_frame(&fx, (audio_mixer_frame_t) { 0 });
    assert(tail.left > 4000);
    assert(tail.right > 4000);

    for (int i = 0; i < 2100; i++) {
        (void)audio_delay_fx_process_frame(&fx, (audio_mixer_frame_t) { 0 });
    }
    assert(!audio_delay_fx_is_ringing(&fx));

    audio_mixer_frame_t in = { .left = 777, .right = -777 };
    audio_mixer_frame_t out = audio_delay_fx_process_frame(&fx, in);
    assert(out.left == in.left);
    assert(out.right == in.right);
}

static void test_reenable_clears_stale_tail(void)
{
    int16_t left[32] = { 0 };
    int16_t right[32] = { 0 };
    audio_delay_fx_t fx;
    audio_delay_fx_init(&fx, left, right, 32u, 1000u);

    audio_delay_fx_config_t cfg = {
        .enabled = true,
        .mode = AUDIO_DELAY_FX_MODE_ECHO,
        .delay_ms = 4,
        .wet_q15 = 16384,
        .feedback_q15 = 8192,
    };
    audio_delay_fx_configure(&fx, &cfg);
    (void)audio_delay_fx_process_frame(
        &fx,
        (audio_mixer_frame_t) { .left = 10000, .right = 10000 });

    cfg.enabled = false;
    audio_delay_fx_configure(&fx, &cfg);
    assert(audio_delay_fx_is_ringing(&fx));

    cfg.enabled = true;
    audio_delay_fx_configure(&fx, &cfg);
    assert(!audio_delay_fx_is_ringing(&fx));
    for (int i = 0; i < 8; i++) {
        audio_mixer_frame_t out = audio_delay_fx_process_frame(&fx, (audio_mixer_frame_t) { 0 });
        assert(out.left == 0);
        assert(out.right == 0);
    }
}

static void test_delay_switch_off_rings_exactly_one_delay_period(void)
{
    int16_t left[32] = { 0 };
    int16_t right[32] = { 0 };
    audio_delay_fx_t fx;
    audio_delay_fx_init(&fx, left, right, 32u, 1000u);

    audio_delay_fx_config_t cfg = {
        .enabled = true,
        .mode = AUDIO_DELAY_FX_MODE_DELAY,
        .delay_ms = 4,
        .wet_q15 = 32767,
        .feedback_q15 = 24576,
    };
    audio_delay_fx_configure(&fx, &cfg);
    (void)audio_delay_fx_process_frame(
        &fx,
        (audio_mixer_frame_t) { .left = 10000, .right = -10000 });

    cfg.enabled = false;
    audio_delay_fx_configure(&fx, &cfg);
    assert(audio_delay_fx_is_ringing(&fx));
    assert(fx.tail_frames_remaining == fx.delay_frames);
    assert(fx.tail_frames_remaining == 4u);

    for (int i = 0; i < 3; i++) {
        audio_mixer_frame_t out = audio_delay_fx_process_frame(&fx, (audio_mixer_frame_t) { 0 });
        assert(out.left == 0);
        assert(out.right == 0);
        assert(audio_delay_fx_is_ringing(&fx));
    }

    audio_mixer_frame_t final_tap = audio_delay_fx_process_frame(
        &fx,
        (audio_mixer_frame_t) { 0 });
    assert(final_tap.left > 9900 && final_tap.left <= 10000);
    assert(final_tap.right < -9900 && final_tap.right >= -10000);
    assert(!audio_delay_fx_is_ringing(&fx));

    audio_mixer_frame_t dry = { .left = 123, .right = -456 };
    audio_mixer_frame_t after_tail = audio_delay_fx_process_frame(&fx, dry);
    assert(after_tail.left == dry.left);
    assert(after_tail.right == dry.right);
}

static void test_disabled_commands_do_not_retime_a_ringing_delay(void)
{
    int16_t left[32] = { 0 };
    int16_t right[32] = { 0 };
    audio_delay_fx_t fx;
    audio_delay_fx_init(&fx, left, right, 32u, 1000u);

    audio_delay_fx_config_t active = {
        .enabled = true,
        .mode = AUDIO_DELAY_FX_MODE_DELAY,
        .delay_ms = 4,
        .wet_q15 = 32767,
        .feedback_q15 = 0,
    };
    audio_delay_fx_configure(&fx, &active);
    (void)audio_delay_fx_process_frame(
        &fx,
        (audio_mixer_frame_t) { .left = 10000, .right = -10000 });

    /* Simulate a target/beat/depth change arriving with the lane disabled.
     * The pending tap must retain the mode, timing and wet gain that wrote it. */
    audio_delay_fx_config_t unrelated_disable = {
        .enabled = false,
        .mode = AUDIO_DELAY_FX_MODE_ECHO,
        .delay_ms = 2,
        .wet_q15 = 1024,
        .feedback_q15 = 20000,
    };
    audio_delay_fx_configure(&fx, &unrelated_disable);
    assert(fx.config.mode == AUDIO_DELAY_FX_MODE_DELAY);
    assert(fx.config.delay_ms == active.delay_ms);
    assert(fx.config.wet_q15 == active.wet_q15);
    assert(fx.config.feedback_q15 == 0u);
    assert(fx.delay_frames == 4u);
    assert(fx.tail_frames_remaining == 4u);

    audio_mixer_frame_t first = audio_delay_fx_process_frame(
        &fx,
        (audio_mixer_frame_t) { 0 });
    assert(first.left == 0);
    assert(first.right == 0);

    /* A second disabled command during the tail must not alter it either. */
    unrelated_disable.delay_ms = 8;
    unrelated_disable.wet_q15 = 16384;
    audio_delay_fx_configure(&fx, &unrelated_disable);
    assert(fx.config.delay_ms == active.delay_ms);
    assert(fx.config.wet_q15 == active.wet_q15);
    assert(fx.tail_frames_remaining == 3u);

    for (int i = 0; i < 2; i++) {
        audio_mixer_frame_t out = audio_delay_fx_process_frame(
            &fx,
            (audio_mixer_frame_t) { 0 });
        assert(out.left == 0);
        assert(out.right == 0);
    }
    audio_mixer_frame_t tap = audio_delay_fx_process_frame(
        &fx,
        (audio_mixer_frame_t) { 0 });
    assert(tap.left > 9900 && tap.left <= 10000);
    assert(tap.right < -9900 && tap.right >= -10000);
    assert(!audio_delay_fx_is_ringing(&fx));
}

static void assert_silent_frames(audio_delay_fx_t *fx, int count)
{
    for (int i = 0; i < count; i++) {
        audio_mixer_frame_t out = audio_delay_fx_process_frame(
            fx,
            (audio_mixer_frame_t) { 0 });
        assert(out.left == 0);
        assert(out.right == 0);
    }
}

static void test_live_echo_delay_mode_changes_clear_shared_line(void)
{
    int16_t left[32] = { 0 };
    int16_t right[32] = { 0 };
    audio_delay_fx_t fx;
    audio_delay_fx_init(&fx, left, right, 32u, 1000u);

    audio_delay_fx_config_t echo = {
        .enabled = true,
        .mode = AUDIO_DELAY_FX_MODE_ECHO,
        .delay_ms = 4,
        .wet_q15 = 32767,
        .feedback_q15 = 16384,
    };
    audio_delay_fx_config_t delay = {
        .enabled = true,
        .mode = AUDIO_DELAY_FX_MODE_DELAY,
        .delay_ms = 4,
        .wet_q15 = 32767,
        .feedback_q15 = 16384,
    };

    /* Switch before ECHO's pending first tap reaches the read head. */
    audio_delay_fx_configure(&fx, &echo);
    (void)audio_delay_fx_process_frame(
        &fx,
        (audio_mixer_frame_t) { .left = 12000, .right = -12000 });
    for (int i = 0; i < 3; i++) {
        (void)audio_delay_fx_process_frame(&fx, (audio_mixer_frame_t) { 0 });
    }
    audio_delay_fx_configure(&fx, &delay);
    assert(fx.config.mode == AUDIO_DELAY_FX_MODE_DELAY);
    assert(fx.config.feedback_q15 == 0u);
    assert_silent_frames(&fx, 8);

    /* Exercise the reverse transition too: a pending one-shot DELAY tap must
     * not seed ECHO's newly enabled feedback path. */
    (void)audio_delay_fx_process_frame(
        &fx,
        (audio_mixer_frame_t) { .left = -9000, .right = 9000 });
    for (int i = 0; i < 3; i++) {
        (void)audio_delay_fx_process_frame(&fx, (audio_mixer_frame_t) { 0 });
    }
    audio_delay_fx_configure(&fx, &echo);
    assert(fx.config.mode == AUDIO_DELAY_FX_MODE_ECHO);
    assert(fx.config.feedback_q15 == echo.feedback_q15);
    assert_silent_frames(&fx, 12);
}

static void test_null_or_zero_buffer_bypasses(void)
{
    audio_delay_fx_t fx;
    audio_delay_fx_init(&fx, NULL, NULL, 0u, 1000u);

    audio_delay_fx_config_t cfg = {
        .enabled = true,
        .mode = AUDIO_DELAY_FX_MODE_DELAY,
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
    test_switch_off_rings_tail_then_goes_silent();
    test_reenable_clears_stale_tail();
    test_delay_switch_off_rings_exactly_one_delay_period();
    test_disabled_commands_do_not_retime_a_ringing_delay();
    test_live_echo_delay_mode_changes_clear_shared_line();
    test_null_or_zero_buffer_bypasses();
    puts("audio_delay_fx tests passed");
    return 0;
}
