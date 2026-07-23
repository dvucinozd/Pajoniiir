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

    /* Dry is unity, as on a hardware flanger. The delay line was just cleared
     * by the engage reset, so the first frame carries no wet component and
     * passes through untouched - it is also well below the soft-clip knee. */
    audio_mixer_frame_t first = audio_flanger_fx_process_frame(
        &fx, (audio_mixer_frame_t) { .left = 16000, .right = 16000 });
    assert(first.left == 16000);
    assert(first.right == 16000);

    int first_wet_frame = -1;
    for (int i = 1; i < 400; ++i) {
        audio_mixer_frame_t out = audio_flanger_fx_process_frame(
            &fx, (audio_mixer_frame_t) { 0 });
        if (out.left != 0 || out.right != 0) {
            first_wet_frame = i;
            break;
        }
    }

    /* 250 us..6 ms at 48 kHz is frame 12..288; the LFO starts at the minimum. */
    assert(first_wet_frame >= 10);
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

/* The comb notch is the whole point of a flanger, and its depth is set by how
 * closely the wet copy cancels the dry one. "output differs from input" passes
 * even at a barely-audible -6 dB, which is how a shallow mix shipped unnoticed;
 * this asserts the effect actually cancels somewhere in the sweep. */
static void test_sweep_produces_a_deep_notch(void)
{
    audio_flanger_fx_t fx;
    make_fx(&fx);
    audio_flanger_fx_configure(&fx, &(audio_flanger_fx_config_t) {
        .enabled = true,
        .period_ms = 300,
        .depth_q15 = 32767,
    });

    /* 400 Hz: its first null needs a 1.25 ms delay, comfortably inside the
     * 250 us - 6 ms sweep, so a working flanger must pass through
     * cancellation. Amplitude is kept low on purpose: the resonant peak of
     * this tuning is 3.34x, so a louder test tone would run into the soft-clip
     * knee and the measurement would be of the knee, not of the comb. */
    const double amp = 3000.0;
    const double w = 2.0 * 3.14159265358979 * 400.0 / (double)TEST_SAMPLE_RATE;
    const int window = 128;

    double weakest = amp;      /* smallest windowed peak seen */
    double strongest = 0.0;
    double win_peak = 0.0;

    /* Two full LFO periods at 300 ms, plus a lead-in for the ramps to settle. */
    const int total = (TEST_SAMPLE_RATE * 700) / 1000;
    for (int i = 0; i < total; ++i) {
        int16_t sample = (int16_t)(sin((double)i * w) * amp);
        audio_mixer_frame_t out = audio_flanger_fx_process_frame(
            &fx, (audio_mixer_frame_t) { .left = sample, .right = sample });
        double mag = fabs((double)out.left);
        if (mag > win_peak) win_peak = mag;
        if ((i % window) == window - 1) {
            if (i > (int)(TEST_SAMPLE_RATE / 10u)) {   /* skip the settling lead-in */
                if (win_peak < weakest) weakest = win_peak;
                if (win_peak > strongest) strongest = win_peak;
            }
            win_peak = 0.0;
        }
    }

    /* At least 12 dB between the sweep's null and its peak. A -6 dB mix, the
     * value this test was written against, reaches only about 6 dB. */
    assert(strongest > 0.0);
    assert(weakest < strongest * 0.25);

    /* Dry is unity and the wet is added on top, so a resonant flanger peaks
     * well above the input: the theoretical ceiling is 1 + wet/(1-fb) = 3.8x
     * and the measured peak is 2.97x at this 300 ms sweep (3.34x at a slower
     * one, where the resonance has longer to build at each delay). Bound it on
     * both sides - the lower bound is what stops the resonance from being
     * quietly tuned away again, the upper bound catches runaway. */
    assert(strongest > amp * 2.8);
    assert(strongest <= amp * 3.6);
}

/* The 3.34x resonant gain means loud material would otherwise hard-clip inside
 * the effect, ahead of the master limiter, where nothing downstream can catch
 * it. The soft knee has to absorb that without touching the quiet case, since
 * the quiet case is the tuning that was accepted by ear. */
static void test_soft_knee_is_transparent_below_it_and_bounds_above(void)
{
    audio_flanger_fx_t fx;
    make_fx(&fx);
    audio_flanger_fx_configure(&fx, &(audio_flanger_fx_config_t) {
        .enabled = true,
        .period_ms = 300,
        .depth_q15 = 32767,
    });

    /* First frame: delay line is empty, so the output is the dry sample alone.
     * Below the 24576 knee it must pass through bit-exact. */
    audio_mixer_frame_t quiet = audio_flanger_fx_process_frame(
        &fx, (audio_mixer_frame_t) { .left = 20000, .right = -20000 });
    assert(quiet.left == 20000);
    assert(quiet.right == -20000);

    /* Drive it hard for long enough for the feedback to build, and confirm the
     * knee holds the output inside full scale rather than wrapping. */
    const double w = 2.0 * 3.14159265358979 * 400.0 / (double)TEST_SAMPLE_RATE;
    const int total = (TEST_SAMPLE_RATE * 700) / 1000;
    for (int i = 0; i < total; ++i) {
        int16_t sample = (int16_t)(sin((double)i * w) * 26000.0);
        audio_mixer_frame_t out = audio_flanger_fx_process_frame(
            &fx, (audio_mixer_frame_t) { .left = sample, .right = sample });
        /* int16 cannot exceed full scale, but it can wrap sign if the maths
         * overflows on the way here - that is the failure this catches. */
        assert(!(sample > 20000 && out.left < -20000));
        assert(!(sample < -20000 && out.left > 20000));
    }
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

static void test_full_scale_fractional_interpolation_does_not_overflow(void)
{
    audio_flanger_fx_t fx;
    make_fx(&fx);
    audio_flanger_fx_configure(&fx, &(audio_flanger_fx_config_t) {
        .enabled = true,
        .period_ms = 333,
        .depth_q15 = 32767,
    });
    for (uint32_t i = 0; i < TEST_CAPACITY; i++) {
        buffer_left[i] = (i & 1u) ? INT16_MAX : INT16_MIN;
        buffer_right[i] = buffer_left[i];
    }

    uint32_t phase = fx.lfo_phase_q32 + fx.lfo_step_q32;
    uint32_t tri_q16 = phase < 0x80000000u ? (phase >> 15)
                                               : ((0xFFFFFFFFu - phase) >> 15);
    uint32_t delay_q16 = fx.min_delay_q16 +
        (uint32_t)(((uint64_t)fx.span_delay_q16 * tri_q16) >> 16);
    uint32_t delay_int = delay_q16 >> 16;
    uint32_t frac_q16 = delay_q16 & 0xFFFFu;
    uint32_t idx0 = fx.write_index >= delay_int
        ? fx.write_index - delay_int
        : fx.write_index + fx.capacity_frames - delay_int;
    uint32_t idx1 = idx0 == 0u ? fx.capacity_frames - 1u : idx0 - 1u;
    int32_t s0 = buffer_left[idx0];
    int32_t s1 = buffer_left[idx1];
    int32_t delayed = s0 +
        (int32_t)(((int64_t)(s1 - s0) * frac_q16) >> 16);
    /* Dry is zero here, so the output is the wet path alone. At wet <= 0.70 a
     * full-scale delayed sample stays under the soft-clip knee, so the knee is
     * the identity and the expected value is the raw wet mix. */
    int32_t expected = (delayed * (int32_t)fx.wet_cur_q15) >> 15;
    if (expected > INT16_MAX) expected = INT16_MAX;
    if (expected < INT16_MIN) expected = INT16_MIN;

    audio_mixer_frame_t out = audio_flanger_fx_process_frame(
        &fx, (audio_mixer_frame_t) { 0 });
    assert(out.left == (int16_t)expected);
    assert(out.right == (int16_t)expected);
}

int main(void)
{
    test_required_frames_fit_max_delay();
    test_disabled_bypasses_input();
    test_unallocated_bypasses_input();
    test_zero_depth_is_transparent();
    test_impulse_reappears_within_delay_bounds();
    test_enabled_flanger_colours_a_tone();
    test_sweep_produces_a_deep_notch();
    test_soft_knee_is_transparent_below_it_and_bounds_above();
    test_reenable_clears_stale_buffer();
    test_full_scale_fractional_interpolation_does_not_overflow();
    puts("audio_flanger_fx tests passed");
    return 0;
}
