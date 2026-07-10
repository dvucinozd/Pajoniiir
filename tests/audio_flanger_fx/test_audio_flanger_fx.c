#include "audio_flanger_fx.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define TEST_SAMPLE_RATE 48000u
#define TEST_CAPACITY 512u

static int16_t buffer_left[TEST_CAPACITY];
static int16_t buffer_right[TEST_CAPACITY];

static void make_fx(audio_flanger_fx_t *fx)
{
    memset(buffer_left, 0, sizeof(buffer_left));
    memset(buffer_right, 0, sizeof(buffer_right));
    audio_flanger_fx_init(fx, buffer_left, buffer_right, TEST_CAPACITY, TEST_SAMPLE_RATE);
}

static void test_required_frames_fit_max_delay(void)
{
    uint32_t frames = audio_flanger_fx_required_frames(TEST_SAMPLE_RATE);
    /* 6 ms at 48 kHz = 288 frames + interpolation guard. */
    assert(frames >= 290u);
    assert(frames <= TEST_CAPACITY);
}

static void test_disabled_bypasses_input(void)
{
    audio_flanger_fx_t fx;
    make_fx(&fx);

    audio_mixer_frame_t in = { .left = 4321, .right = -1234 };
    audio_mixer_frame_t out = audio_flanger_fx_process_frame(&fx, in);
    assert(out.left == in.left);
    assert(out.right == in.right);
}

static void test_unallocated_bypasses_input(void)
{
    audio_flanger_fx_t fx;
    audio_flanger_fx_init(&fx, NULL, NULL, 0u, TEST_SAMPLE_RATE);
    audio_flanger_fx_configure(&fx, &(audio_flanger_fx_config_t) {
        .enabled = true,
        .period_ms = 500,
        .depth_q15 = 32767,
    });

    audio_mixer_frame_t in = { .left = 4321, .right = -1234 };
    audio_mixer_frame_t out = audio_flanger_fx_process_frame(&fx, in);
    assert(!audio_flanger_fx_is_allocated(&fx));
    assert(out.left == in.left);
    assert(out.right == in.right);
}

static void test_zero_depth_is_transparent(void)
{
    audio_flanger_fx_t fx;
    make_fx(&fx);
    audio_flanger_fx_configure(&fx, &(audio_flanger_fx_config_t) {
        .enabled = true,
        .period_ms = 500,
        .depth_q15 = 0,
    });

    for (int i = 0; i < 400; ++i) {
        int16_t sample = (int16_t)(sinf((float)i * 0.05f) * 12000.0f);
        audio_mixer_frame_t out = audio_flanger_fx_process_frame(
            &fx, (audio_mixer_frame_t) { .left = sample, .right = sample });
        assert(out.left == sample);
        assert(out.right == sample);
    }
}

static void test_impulse_reappears_within_delay_bounds(void)
{
    audio_flanger_fx_t fx;
    make_fx(&fx);
    audio_flanger_fx_configure(&fx, &(audio_flanger_fx_config_t) {
        .enabled = true,
        .period_ms = 2000,
        .depth_q15 = 32767,
    });

    audio_mixer_frame_t first = audio_flanger_fx_process_frame(
        &fx, (audio_mixer_frame_t) { .left = 16000, .right = 16000 });
    assert(first.left == 16000);

    int first_wet_frame = -1;
    for (int i = 1; i < 400; ++i) {
        audio_mixer_frame_t out = audio_flanger_fx_process_frame(
            &fx, (audio_mixer_frame_t) { 0 });
        if (out.left != 0 || out.right != 0) {
            first_wet_frame = i;
            break;
        }
    }

    /* 0.6 ms..6 ms at 48 kHz is roughly frame 28..289. */
    assert(first_wet_frame >= 26);
    assert(first_wet_frame <= 292);
}

static void test_enabled_flanger_colours_a_tone(void)
{
    audio_flanger_fx_t fx;
    make_fx(&fx);
    audio_flanger_fx_configure(&fx, &(audio_flanger_fx_config_t) {
        .enabled = true,
        .period_ms = 300,
        .depth_q15 = 32767,
    });

    int changed = 0;
    for (int i = 0; i < 4800; ++i) {
        int16_t sample = (int16_t)(sinf((float)i * 0.13f) * 10000.0f);
        audio_mixer_frame_t out = audio_flanger_fx_process_frame(
            &fx, (audio_mixer_frame_t) { .left = sample, .right = sample });
        if (out.left != sample) {
            changed++;
        }
    }
    assert(changed > 4000);
}

static void test_reenable_clears_stale_buffer(void)
{
    audio_flanger_fx_t fx;
    make_fx(&fx);
    audio_flanger_fx_config_t cfg = {
        .enabled = true,
        .period_ms = 500,
        .depth_q15 = 32767,
    };
    audio_flanger_fx_configure(&fx, &cfg);
    for (int i = 0; i < 64; ++i) {
        (void)audio_flanger_fx_process_frame(
            &fx, (audio_mixer_frame_t) { .left = 16000, .right = -16000 });
    }

    cfg.enabled = false;
    audio_flanger_fx_configure(&fx, &cfg);
    cfg.enabled = true;
    audio_flanger_fx_configure(&fx, &cfg);

    for (int i = 0; i < 400; ++i) {
        audio_mixer_frame_t out = audio_flanger_fx_process_frame(
            &fx, (audio_mixer_frame_t) { 0 });
        assert(out.left == 0);
        assert(out.right == 0);
    }
}

int main(void)
{
    test_required_frames_fit_max_delay();
    test_disabled_bypasses_input();
    test_unallocated_bypasses_input();
    test_zero_depth_is_transparent();
    test_impulse_reappears_within_delay_bounds();
    test_enabled_flanger_colours_a_tone();
    test_reenable_clears_stale_buffer();
    puts("audio_flanger_fx tests passed");
    return 0;
}
