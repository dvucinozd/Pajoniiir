#include "audio_pad_fx.h"

#include <string.h>

typedef struct {
    audio_pad_fx_kind_t kind;
    uint16_t filter_raw;
    uint32_t echo_delay_ms;
    uint16_t echo_wet_q15;
    uint16_t echo_feedback_q15;
} audio_pad_fx_preset_t;

static int16_t clamp_i16(int32_t value)
{
    if (value > 32767) return 32767;
    if (value < -32768) return -32768;
    return (int16_t)value;
}

static audio_mixer_frame_t mix_clamped(audio_mixer_frame_t a, audio_mixer_frame_t b)
{
    return (audio_mixer_frame_t) {
        .left = clamp_i16((int32_t)a.left + (int32_t)b.left),
        .right = clamp_i16((int32_t)a.right + (int32_t)b.right),
    };
}

static audio_pad_fx_preset_t preset_for(audio_pad_fx_mode_t mode, uint8_t pad)
{
    if (mode == AUDIO_PAD_FX_MODE_PAD_FX2) {
        switch (pad) {
        case 0:
            return (audio_pad_fx_preset_t) {
                .kind = AUDIO_PAD_FX_KIND_FILTER,
                .filter_raw = 2300u,
            };
        case 1:
            return (audio_pad_fx_preset_t) {
                .kind = AUDIO_PAD_FX_KIND_FILTER,
                .filter_raw = 14000u,
            };
        case 2:
            return (audio_pad_fx_preset_t) {
                .kind = AUDIO_PAD_FX_KIND_ECHO,
                .echo_delay_ms = 125u,
                .echo_wet_q15 = 9830u,
                .echo_feedback_q15 = 9830u,
            };
        case 3:
            return (audio_pad_fx_preset_t) {
                .kind = AUDIO_PAD_FX_KIND_ECHO,
                .echo_delay_ms = 1000u,
                .echo_wet_q15 = 9830u,
                .echo_feedback_q15 = 13107u,
            };
        default:
            return (audio_pad_fx_preset_t) { .kind = AUDIO_PAD_FX_KIND_NONE };
        }
    }

    switch (pad) {
    case 0:
        return (audio_pad_fx_preset_t) {
            .kind = AUDIO_PAD_FX_KIND_FILTER,
            .filter_raw = 3600u,
        };
    case 1:
        return (audio_pad_fx_preset_t) {
            .kind = AUDIO_PAD_FX_KIND_FILTER,
            .filter_raw = 12700u,
        };
    case 2:
        return (audio_pad_fx_preset_t) {
            .kind = AUDIO_PAD_FX_KIND_ECHO,
            .echo_delay_ms = 250u,
            .echo_wet_q15 = 8192u,
            .echo_feedback_q15 = 9830u,
        };
    case 3:
        return (audio_pad_fx_preset_t) {
            .kind = AUDIO_PAD_FX_KIND_ECHO,
            .echo_delay_ms = 500u,
            .echo_wet_q15 = 8192u,
            .echo_feedback_q15 = 11469u,
        };
    default:
        return (audio_pad_fx_preset_t) { .kind = AUDIO_PAD_FX_KIND_NONE };
    }
}

void audio_pad_fx_init(audio_pad_fx_state_t *fx, uint32_t sample_rate_hz)
{
    audio_pad_fx_init_with_echo_buffer(fx, sample_rate_hz, 0, 0, 0);
}

void audio_pad_fx_init_with_echo_buffer(audio_pad_fx_state_t *fx,
                                        uint32_t sample_rate_hz,
                                        int16_t *echo_left,
                                        int16_t *echo_right,
                                        uint32_t echo_capacity_frames)
{
    if (!fx) return;
    memset(fx, 0, sizeof(*fx));
    audio_filter_init(&fx->filter, sample_rate_hz);
    audio_delay_fx_init(&fx->echo,
                        echo_left,
                        echo_right,
                        echo_capacity_frames,
                        sample_rate_hz ? sample_rate_hz : 44100u);
    fx->kind = AUDIO_PAD_FX_KIND_NONE;
    fx->config.mode = AUDIO_PAD_FX_MODE_PAD_FX1;
}

void audio_pad_fx_reset(audio_pad_fx_state_t *fx)
{
    if (!fx) return;
    fx->active = false;
    fx->echo_tail_active = false;
    fx->echo_tail_frames_remaining = 0;
    fx->kind = AUDIO_PAD_FX_KIND_NONE;
    fx->config.active = false;
    audio_filter_set_raw(&fx->filter, AUDIO_FILTER_RAW_CENTER);
    audio_filter_reset(&fx->filter);
    audio_delay_fx_configure(&fx->echo, &(audio_delay_fx_config_t) { 0 });
    audio_delay_fx_reset(&fx->echo);
}

void audio_pad_fx_set(audio_pad_fx_state_t *fx, audio_pad_fx_config_t config)
{
    if (!fx) return;

    if (!config.active) {
        if (fx->active &&
            fx->config.mode == config.mode &&
            fx->config.pad == config.pad) {
            if (fx->kind == AUDIO_PAD_FX_KIND_ECHO && audio_delay_fx_is_allocated(&fx->echo)) {
                fx->active = false;
                fx->config.active = false;
                fx->echo_tail_active = true;
                /* 2 s of tail so even the 1-beat/1 s echo pads ring out. */
                fx->echo_tail_frames_remaining = 2u * (fx->echo.sample_rate ? fx->echo.sample_rate : 44100u);
                return;
            }
            audio_pad_fx_reset(fx);
        }
        return;
    }

    audio_pad_fx_preset_t preset = preset_for(config.mode, config.pad);
    fx->config = config;
    fx->kind = preset.kind;
    fx->active = preset.kind != AUDIO_PAD_FX_KIND_NONE;
    fx->config.active = fx->active;
    fx->echo_tail_active = false;
    fx->echo_tail_frames_remaining = 0;

    if (preset.kind == AUDIO_PAD_FX_KIND_FILTER) {
        audio_filter_set_raw(&fx->filter, preset.filter_raw);
        audio_delay_fx_configure(&fx->echo, &(audio_delay_fx_config_t) { 0 });
    } else if (preset.kind == AUDIO_PAD_FX_KIND_ECHO) {
        audio_filter_set_raw(&fx->filter, AUDIO_FILTER_RAW_CENTER);
        audio_filter_reset(&fx->filter);
        audio_delay_fx_configure(&fx->echo, &(audio_delay_fx_config_t) {
            .enabled = audio_delay_fx_is_allocated(&fx->echo),
            .delay_ms = preset.echo_delay_ms,
            .wet_q15 = preset.echo_wet_q15,
            .feedback_q15 = preset.echo_feedback_q15,
        });
    } else {
        audio_pad_fx_reset(fx);
    }
}

audio_mixer_frame_t audio_pad_fx_process_frame(audio_pad_fx_state_t *fx,
                                               audio_mixer_frame_t in)
{
    if (!fx) {
        return in;
    }

    if (fx->active && fx->kind == AUDIO_PAD_FX_KIND_FILTER) {
        return audio_filter_process_frame(&fx->filter, true, in);
    }
    if (fx->active && fx->kind == AUDIO_PAD_FX_KIND_ECHO) {
        return audio_delay_fx_process_frame(&fx->echo, in);
    }
    if (fx->echo_tail_active) {
        audio_mixer_frame_t tail = audio_delay_fx_process_frame(&fx->echo, (audio_mixer_frame_t) { 0 });
        if (fx->echo_tail_frames_remaining > 0u) {
            fx->echo_tail_frames_remaining--;
        }
        if (fx->echo_tail_frames_remaining == 0u) {
            audio_pad_fx_reset(fx);
        }
        return mix_clamped(in, tail);
    }
    return in;
}

bool audio_pad_fx_is_active(const audio_pad_fx_state_t *fx)
{
    return fx && fx->active;
}

audio_pad_fx_kind_t audio_pad_fx_kind(const audio_pad_fx_state_t *fx)
{
    return fx ? fx->kind : AUDIO_PAD_FX_KIND_NONE;
}
