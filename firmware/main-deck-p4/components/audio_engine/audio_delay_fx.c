#include "audio_delay_fx.h"

#include <string.h>

/* 2*pi*4.5 kHz — cutoff of the one-pole damping filter in the feedback path.
 * Each repeat passes through it once, so the echo darkens per generation
 * (tape-style) instead of repeating at full bandwidth. */
#define AUDIO_DELAY_FX_DAMP_OMEGA 28274u
/* How long the tail keeps ringing after the effect is switched off. */
#define AUDIO_DELAY_FX_TAIL_SECONDS 2u
/* Gain ramps move 1/64th of the remaining distance per frame (~1.5 ms
 * time constant at 44.1 kHz). */
#define AUDIO_DELAY_FX_SMOOTH_SHIFT 6
/* Soft-clip knee, matching audio_flanger_fx. Dry is unity and wet is added on
 * top, so a sustained signal builds to 1 + wet/(1-feedback): 3.18x measured
 * for ECHO at full depth, 1.70x for DELAY, which has no feedback. A hard clamp
 * there squares the signal off *inside* the effect, ahead of the master
 * limiter, where nothing downstream can catch it - measured at 47% of samples
 * pinned for ECHO on a signal at 49% of full scale. Below the knee this is the
 * identity, so the tuning that was accepted by ear is unchanged. */
#define AUDIO_DELAY_FX_KNEE 24576   /* 0.75 FS */

/* Quadratic soft knee: unity gain and unity slope up to the knee, then the
 * gain rolls off smoothly to zero exactly at full scale, so there is no slope
 * discontinuity to buzz. Above 2*FS-knee it saturates flat. */
static int16_t soft_clip(int32_t value)
{
    const int32_t knee = AUDIO_DELAY_FX_KNEE;
    const int32_t span = 32767 - knee;
    int32_t sign = value < 0 ? -1 : 1;
    int32_t mag = value < 0 ? -value : value;
    if (mag <= knee) {
        return (int16_t)value;
    }
    int32_t over = mag - knee;
    if (over >= 2 * span) {
        mag = 32767;
    } else {
        mag = knee + over - (over * over) / (4 * span);
    }
    return (int16_t)(sign * mag);
}

static int32_t q15_mul_i32(int32_t sample, uint16_t gain_q15)
{
    return (sample * (int32_t)gain_q15) >> 15;
}

static uint16_t smooth_q15(uint16_t current, uint16_t target)
{
    int32_t delta = (int32_t)target - (int32_t)current;
    int32_t step = delta / (1 << AUDIO_DELAY_FX_SMOOTH_SHIFT);
    if (step == 0 && delta != 0) {
        step = delta > 0 ? 1 : -1;
    }
    return (uint16_t)((int32_t)current + step);
}

void audio_delay_fx_init(audio_delay_fx_t *fx,
                         int16_t *left,
                         int16_t *right,
                         uint32_t capacity_frames,
                         uint32_t sample_rate)
{
    if (!fx) return;
    memset(fx, 0, sizeof(*fx));
    fx->left = left;
    fx->right = right;
    fx->capacity_frames = capacity_frames;
    fx->sample_rate = sample_rate;
    fx->allocated = left && right && capacity_frames > 1u && sample_rate > 0u;
    audio_delay_fx_reset(fx);
}

void audio_delay_fx_reset(audio_delay_fx_t *fx)
{
    if (!fx) return;
    fx->write_index = 0;
    fx->fb_lp[0] = 0;
    fx->fb_lp[1] = 0;
    fx->wet_cur_q15 = 0;
    fx->feedback_cur_q15 = 0;
    fx->tail_frames_remaining = 0;
    if (fx->left && fx->capacity_frames > 0u) {
        memset(fx->left, 0, fx->capacity_frames * sizeof(fx->left[0]));
    }
    if (fx->right && fx->capacity_frames > 0u) {
        memset(fx->right, 0, fx->capacity_frames * sizeof(fx->right[0]));
    }
}

void audio_delay_fx_configure(audio_delay_fx_t *fx, const audio_delay_fx_config_t *config)
{
    if (!fx || !config) return;
    bool was_enabled = fx->config.enabled;
    bool was_ringing = fx->tail_frames_remaining > 0u;
    audio_delay_fx_mode_t previous_mode = fx->config.mode;
    audio_delay_fx_config_t next = *config;
    if (next.mode != AUDIO_DELAY_FX_MODE_ECHO &&
        next.mode != AUDIO_DELAY_FX_MODE_DELAY) {
        next.mode = AUDIO_DELAY_FX_MODE_ECHO;
    }
    if (next.wet_q15 > 32767u) next.wet_q15 = 32767u;
    if (next.feedback_q15 > 24576u) next.feedback_q15 = 24576u;
    if (next.mode == AUDIO_DELAY_FX_MODE_DELAY) {
        /* DELAY is a single full-band tap; ECHO owns the damped feedback
         * behaviour.  Enforce that distinction inside the DSP as well as at
         * the audio-engine API boundary. */
        next.feedback_q15 = 0u;
    }

    if (!next.enabled && (was_enabled || was_ringing)) {
        /* The tail belongs to the configuration that filled the line. Control
         * changes can publish another disabled command while it is ringing
         * (for example after changing target, beat or depth); do not let that
         * command retime or recolour an already buffered ECHO/DELAY tail. */
        next.mode = fx->config.mode;
        next.delay_ms = fx->config.delay_ms;
        next.wet_q15 = fx->config.wet_q15;
        next.feedback_q15 = fx->config.feedback_q15;
    }

    if (next.enabled && (!was_enabled || next.mode != previous_mode)) {
        /* Fresh engage: drop any stale tail and start the gains at their
         * targets. A live ECHO/DELAY mode change also resets the shared line
         * so old feedback cannot leak into a one-shot delay (or vice versa). */
        audio_delay_fx_reset(fx);
        fx->wet_cur_q15 = next.wet_q15;
        fx->feedback_cur_q15 = next.feedback_q15;
    } else if (!next.enabled && was_enabled && fx->allocated) {
        /* Switch-off: ECHO keeps its bounded feedback tail. DELAY only needs
         * enough processing for the one pending tap to leave the line. */
        fx->tail_frames_remaining = previous_mode == AUDIO_DELAY_FX_MODE_DELAY
            ? fx->delay_frames
            : fx->sample_rate * AUDIO_DELAY_FX_TAIL_SECONDS;
    }

    fx->config = next;

    uint64_t frames = ((uint64_t)fx->sample_rate * (uint64_t)fx->config.delay_ms + 999u) / 1000u;
    if (frames < 1u) frames = 1u;
    if (fx->capacity_frames > 0u && frames >= fx->capacity_frames) {
        frames = fx->capacity_frames - 1u;
    }
    fx->delay_frames = (uint32_t)frames;

    uint32_t fs = fx->sample_rate ? fx->sample_rate : 44100u;
    fx->damp_alpha_q15 = (uint16_t)((32768u * AUDIO_DELAY_FX_DAMP_OMEGA) /
                                    (AUDIO_DELAY_FX_DAMP_OMEGA + fs));
}

static int32_t damp_feedback_sample(audio_delay_fx_t *fx, int16_t delayed, uint8_t channel)
{
    fx->fb_lp[channel] += (((int32_t)delayed - fx->fb_lp[channel]) *
                           (int32_t)fx->damp_alpha_q15) >> 15;
    return fx->fb_lp[channel];
}

audio_mixer_frame_t audio_delay_fx_process_frame(audio_delay_fx_t *fx, audio_mixer_frame_t in)
{
    if (!fx || !fx->allocated || fx->delay_frames == 0u) {
        return in;
    }
    bool active = fx->config.enabled;
    bool ringing = fx->tail_frames_remaining > 0u;
    if (!active && !ringing) {
        return in;
    }

    fx->wet_cur_q15 = smooth_q15(fx->wet_cur_q15, fx->config.wet_q15);
    fx->feedback_cur_q15 = smooth_q15(fx->feedback_cur_q15, fx->config.feedback_q15);

    uint32_t read_index = fx->write_index >= fx->delay_frames
        ? fx->write_index - fx->delay_frames
        : fx->write_index + fx->capacity_frames - fx->delay_frames;
    int16_t delayed_l = fx->left[read_index];
    int16_t delayed_r = fx->right[read_index];

    int32_t out_l = (int32_t)in.left + q15_mul_i32(delayed_l, fx->wet_cur_q15);
    int32_t out_r = (int32_t)in.right + q15_mul_i32(delayed_r, fx->wet_cur_q15);

    int32_t fb_l = q15_mul_i32(damp_feedback_sample(fx, delayed_l, 0), fx->feedback_cur_q15);
    int32_t fb_r = q15_mul_i32(damp_feedback_sample(fx, delayed_r, 1), fx->feedback_cur_q15);
    /* While ringing out, the input no longer feeds the line — only the
     * damped feedback keeps circulating until the tail window closes. */
    int32_t write_l = active ? (int32_t)in.left + fb_l : fb_l;
    int32_t write_r = active ? (int32_t)in.right + fb_r : fb_r;
    fx->left[fx->write_index] = soft_clip(write_l);
    fx->right[fx->write_index] = soft_clip(write_r);
    fx->write_index++;
    if (fx->write_index >= fx->capacity_frames) {
        fx->write_index = 0u;
    }

    if (!active && ringing) {
        fx->tail_frames_remaining--;
    }

    return (audio_mixer_frame_t) {
        .left = soft_clip(out_l),
        .right = soft_clip(out_r),
    };
}

bool audio_delay_fx_is_allocated(const audio_delay_fx_t *fx)
{
    return fx && fx->allocated;
}

bool audio_delay_fx_is_ringing(const audio_delay_fx_t *fx)
{
    return fx && fx->tail_frames_remaining > 0u;
}

uint32_t audio_delay_fx_delay_ms(const audio_delay_fx_t *fx)
{
    return fx ? fx->config.delay_ms : 0u;
}
