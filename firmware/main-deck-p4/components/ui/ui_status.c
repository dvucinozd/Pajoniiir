#include "ui_status.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

void ui_status_format_transport_text(ui_status_transport_text_t *out,
                                     uint8_t active_deck,
                                     const deck_state_t *state,
                                     bool loading,
                                     uint8_t load_pct)
{
    if (!out) {
        return;
    }

    memset(out, 0, sizeof(*out));
    if (loading) {
        snprintf(out->text, sizeof(out->text), "D%u LOAD %u%%",
                 (unsigned)active_deck + 1u,
                 (unsigned)load_pct);
        out->kind = UI_STATUS_TRANSPORT_LOADING;
        return;
    }

    bool playing = state && state->playing;
    snprintf(out->text, sizeof(out->text), "D%u %s",
             (unsigned)active_deck + 1u,
             playing ? "PLAY" : "PAUSE");
    out->kind = playing ? UI_STATUS_TRANSPORT_PLAY : UI_STATUS_TRANSPORT_PAUSE;
}

bool ui_status_format_limiter_text(char *out,
                                   size_t out_len,
                                   const audio_mixer_limiter_stats_t *current,
                                   uint32_t previous_limited_samples)
{
    if (!out || out_len == 0 || !current) {
        return false;
    }
    if (current->limited_samples == 0 ||
        current->limited_samples <= previous_limited_samples) {
        return false;
    }

    snprintf(out, out_len, "CLIP %u", (unsigned)current->limited_samples);
    return true;
}

#ifndef UI_STATUS_HOST_TEST

#include "ui_active_deck_leds.h"
#include "ui_theme.h"

#ifndef WIN32
#include "audio_engine.h"
#include "control_link.h"
#endif

typedef struct {
    bool valid;
    char text[80];
} ui_status_text_cache_t;

typedef struct {
    bool valid;
    uint32_t color;
} ui_status_color_cache_t;

static ui_status_widgets_t s_widgets;
static uint32_t s_status_override_until_ms = 0;
static ui_status_text_cache_t s_cache_status_text;
static ui_status_text_cache_t s_cache_header_title;
static ui_status_text_cache_t s_cache_header_artist;
static ui_status_color_cache_t s_cache_status_color;
static ui_status_color_cache_t s_cache_remain_color;
static int s_cache_pitch_centipct = INT_MIN;
static int s_cache_bpm_centi = INT_MIN;
static uint32_t s_cache_elapsed_seconds = UINT32_MAX;
static uint32_t s_cache_remain_seconds = UINT32_MAX;
static bool s_cache_time_loading = false;
static uint32_t s_last_limiter_limited_samples = 0;

static void ui_status_copy_str(char *dst, size_t dst_len, const char *src)
{
    if (!dst || dst_len == 0) {
        return;
    }
    dst[0] = '\0';
    if (!src) {
        return;
    }
    size_t i = 0;
    while (i + 1u < dst_len && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void ui_status_label_set_text_cached(lv_obj_t *label,
                                            ui_status_text_cache_t *cache,
                                            const char *text)
{
    if (!label || !cache) {
        return;
    }
    const char *safe_text = text ? text : "";
    if (cache->valid && strncmp(cache->text, safe_text, sizeof(cache->text)) == 0) {
        return;
    }
    lv_label_set_text(label, safe_text);
    ui_status_copy_str(cache->text, sizeof(cache->text), safe_text);
    cache->valid = true;
}

static void ui_status_obj_set_text_color_cached(lv_obj_t *obj,
                                                ui_status_color_cache_t *cache,
                                                lv_color_t color)
{
    if (!obj || !cache) {
        return;
    }
    uint32_t color_u32 = lv_color_to_u32(color);
    if (cache->valid && cache->color == color_u32) {
        return;
    }
    lv_obj_set_style_text_color(obj, color, LV_PART_MAIN);
    cache->color = color_u32;
    cache->valid = true;
}

static void ui_status_label_set_f2(lv_obj_t *label, float value)
{
    if (!label) {
        return;
    }
    int centi = (int)(value * 100.0f + (value >= 0.0f ? 0.5f : -0.5f));
    if (centi < 0) {
        lv_label_set_text_fmt(label, "-%d.%02d", (-centi) / 100, (-centi) % 100);
    } else {
        lv_label_set_text_fmt(label, "%d.%02d", centi / 100, centi % 100);
    }
}

static void ui_status_set_indicator(const char *text, lv_color_t color)
{
    if (!s_widgets.status_indicator) {
        return;
    }
    ui_status_label_set_text_cached(s_widgets.status_indicator,
                                    &s_cache_status_text,
                                    text ? text : "LOAD ERR");
    ui_status_obj_set_text_color_cached(s_widgets.status_indicator,
                                        &s_cache_status_color,
                                        color);
}

static bool ui_status_has_override(void)
{
    return (int32_t)(s_status_override_until_ms - lv_tick_get()) > 0;
}

void ui_status_init(const ui_status_widgets_t *widgets)
{
    memset(&s_widgets, 0, sizeof(s_widgets));
    if (widgets) {
        s_widgets = *widgets;
    }
    ui_status_invalidate();
}

void ui_status_invalidate_header(void)
{
    s_cache_header_title.valid = false;
    s_cache_header_artist.valid = false;
    s_cache_pitch_centipct = INT_MIN;
    s_cache_bpm_centi = INT_MIN;
    s_cache_elapsed_seconds = UINT32_MAX;
    s_cache_remain_seconds = UINT32_MAX;
    s_cache_time_loading = false;
}

void ui_status_invalidate(void)
{
    s_cache_status_text.valid = false;
    s_cache_status_color.valid = false;
    s_cache_remain_color.valid = false;
    s_last_limiter_limited_samples = 0;
    ui_status_invalidate_header();
}

void ui_status_hold(const char *text, lv_color_t color, uint32_t hold_ms)
{
    s_status_override_until_ms = lv_tick_get() + hold_ms;
    ui_status_set_indicator(text, color);
}

lv_color_t ui_status_color_for_text(const char *status)
{
    if (!status || status[0] == '\0') {
        return COL_RED;
    }
    if (strcmp(status, "AUDIO ERR") == 0 ||
        strcmp(status, "TASK CREATE ERR") == 0 ||
        strcmp(status, "STOP ERR") == 0 ||
        strcmp(status, "NO MEM") == 0 ||
        strcmp(status, "NO AUDIO FRAME") == 0 ||
        strcmp(status, "CODEC OPEN ERR") == 0 ||
        strcmp(status, "NOT FOUND") == 0 ||
        strcmp(status, "LOAD ERR") == 0) {
        return COL_RED;
    }
    if (strcmp(status, "TRACK LOADED") == 0) {
        return COL_GREEN;
    }
    if (strcmp(status, "LOADING") == 0) {
        return COL_ACCENT;
    }
    return COL_TEXT_DIM;
}

void ui_status_set_header_track(const char *title, const char *artist, uint16_t bpm)
{
    if (s_widgets.title) {
        lv_label_set_text(s_widgets.title, title && title[0] ? title : "Unknown Title");
    }
    if (s_widgets.artist) {
        lv_label_set_text(s_widgets.artist, artist && artist[0] ? artist : "Unknown Artist");
    }
    if (s_widgets.bpm) {
        ui_status_label_set_f2(s_widgets.bpm, (float)bpm);
    }
    ui_status_invalidate_header();
}

static void ui_status_update_header_track(const ui_frame_context_t *ctx)
{
    uint8_t idx = ctx->active_deck < DECK_CORE_DECK_COUNT ? ctx->active_deck : 0;
    const ui_deck_track_info_t *info = ctx->deck_info[idx];

    char title[128];
    snprintf(title, sizeof(title), "D%u  %s",
             (unsigned)idx + 1u,
             (info && info->valid && info->title[0]) ? info->title : "No Track");
    ui_status_label_set_text_cached(s_widgets.title, &s_cache_header_title, title);
    ui_status_label_set_text_cached(s_widgets.artist,
                                    &s_cache_header_artist,
                                    (info && info->valid && info->artist[0]) ? info->artist : "");
}

static float ui_status_pitch_percent(const deck_state_t *state)
{
    if (!state) {
        return 0.0f;
    }
#ifndef WIN32
    return deck_core_pitch_percent(state);
#else
    return ((8192.0f - (float)state->pitch) / 8192.0f) * 10.0f;
#endif
}

static void ui_status_update_pitch_bpm(const ui_frame_context_t *ctx)
{
    float pitch_pct = ui_status_pitch_percent(&ctx->active_state);
    int pc = (int)(pitch_pct * 100.0f + (pitch_pct >= 0.0f ? 0.5f : -0.5f));
    if (pc != s_cache_pitch_centipct) {
        s_cache_pitch_centipct = pc;
        lv_label_set_text_fmt(s_widgets.pitch, "%c%d.%02d%%",
                              (pc < 0) ? '-' : '+',
                              (pc < 0 ? -pc : pc) / 100,
                              (pc < 0 ? -pc : pc) % 100);
    }

    float current_bpm = (float)(ctx->active_base_bpm ? ctx->active_base_bpm : 120) *
                        (1.0f + (pitch_pct / 100.0f));
    int bpm_centi = (int)(current_bpm * 100.0f + (current_bpm >= 0.0f ? 0.5f : -0.5f));
    if (bpm_centi != s_cache_bpm_centi) {
        s_cache_bpm_centi = bpm_centi;
        ui_status_label_set_f2(s_widgets.bpm, current_bpm);
    }
}

static void ui_status_update_time(const ui_frame_context_t *ctx)
{
    uint32_t elapsed_ms = ctx->active_state.position_ms;
    uint32_t remain_ms = (ctx->active_duration_ms > elapsed_ms)
                             ? (ctx->active_duration_ms - elapsed_ms)
                             : 0;

    lv_color_t remain_col = COL_TEXT_MUTED;
    if (ctx->active_duration_ms > 0) {
        if (remain_ms <= 10000) {
            remain_col = lv_color_hex(0xFF1744);
        } else if (remain_ms <= 30000) {
            remain_col = lv_color_hex(0xFFAB00);
        }
    }
    ui_status_obj_set_text_color_cached(s_widgets.time_remain,
                                        &s_cache_remain_color,
                                        remain_col);

    if (ctx->ae_loading) {
        if (!s_cache_time_loading) {
            lv_label_set_text(s_widgets.time_elapsed, "LOADING");
            lv_label_set_text(s_widgets.time_remain, "");
            s_cache_time_loading = true;
            s_cache_elapsed_seconds = UINT32_MAX;
            s_cache_remain_seconds = UINT32_MAX;
        }
        return;
    }

    if (s_cache_time_loading) {
        s_cache_time_loading = false;
        s_cache_elapsed_seconds = UINT32_MAX;
        s_cache_remain_seconds = UINT32_MAX;
    }

    uint32_t elapsed_seconds = elapsed_ms / 1000u;
    uint32_t remain_seconds = remain_ms / 1000u;
    if (elapsed_seconds != s_cache_elapsed_seconds) {
        s_cache_elapsed_seconds = elapsed_seconds;
        unsigned hrs = elapsed_seconds / 3600u;
        unsigned mins = (elapsed_seconds % 3600u) / 60u;
        unsigned secs = elapsed_seconds % 60u;
        lv_label_set_text_fmt(s_widgets.time_elapsed, "%02u:%02u:%02u", hrs, mins, secs);
    }
    if (remain_seconds != s_cache_remain_seconds) {
        s_cache_remain_seconds = remain_seconds;
        unsigned hrs = remain_seconds / 3600u;
        unsigned mins = (remain_seconds % 3600u) / 60u;
        unsigned secs = remain_seconds % 60u;
        lv_label_set_text_fmt(s_widgets.time_remain, "-%02u:%02u:%02u", hrs, mins, secs);
    }
}

static void ui_status_update_transport(const ui_frame_context_t *ctx)
{
    ui_status_transport_text_t transport;
    ui_status_format_transport_text(&transport,
                                    ctx->active_deck,
                                    &ctx->active_state,
                                    ctx->ae_loading,
                                    ctx->ae_load_pct);
    if (transport.kind == UI_STATUS_TRANSPORT_LOADING) {
        s_status_override_until_ms = 0;
        ui_status_set_indicator(transport.text, COL_ACCENT);
        s_last_limiter_limited_samples = ctx->mixer_snapshot.limiter.limited_samples;
    } else if (!ui_status_has_override()) {
        char limiter_text[16];
        if (ctx->mixer_snapshot.limiter.limited_samples < s_last_limiter_limited_samples) {
            s_last_limiter_limited_samples = ctx->mixer_snapshot.limiter.limited_samples;
        }
        if (ui_status_format_limiter_text(limiter_text,
                                          sizeof(limiter_text),
                                          &ctx->mixer_snapshot.limiter,
                                          s_last_limiter_limited_samples)) {
            s_last_limiter_limited_samples = ctx->mixer_snapshot.limiter.limited_samples;
            ui_status_set_indicator(limiter_text, COL_RED);
            return;
        }
        ui_status_set_indicator(transport.text,
                                transport.kind == UI_STATUS_TRANSPORT_PLAY ? COL_GREEN : COL_AMBER);
    }
}

#ifndef WIN32
static void ui_status_update_legacy_leds(const ui_frame_context_t *ctx)
{
    static uint8_t s_last_led_state[2][LED_COUNT] = {
        {0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
        {0xFF, 0xFF, 0xFF, 0xFF, 0xFF}
    };

    for (int d = 0; d < 2; d++) {
        const deck_state_t *ds = &ctx->deck_state[d];
        uint32_t duration_ms = ctx->deck_duration_ms[d];

        bool is_active = (d == ctx->active_deck);
        ui_active_deck_leds_t leds =
            ui_active_deck_leds_calculate(ds->playing,
                                          ds->position_ms,
                                          ds->cue_point_ms,
                                          duration_ms,
                                          is_active ? ctx->active_beat_state_valid : false,
                                          is_active ? ctx->active_beat_state.progress_permille : 0);

        bool pfl_on = ctx->mixer_snapshot.pfl_enabled[d];

        const uint8_t next_leds[LED_COUNT] = {
            [LED_CUE] = leds.cue,
            [LED_PLAY] = leds.play,
            [LED_BEAT] = leds.beat,
            [LED_END] = leds.end,
            [LED_PFL] = pfl_on ? 1u : 0u,
        };

        for (int led = 0; led < LED_COUNT; led++) {
            if (next_leds[led] != s_last_led_state[d][led]) {
                s_last_led_state[d][led] = next_leds[led];
                control_link_send_led_deck((led_id_t)led, next_leds[led], d);
            }
        }
    }
}
#endif

void ui_status_update(const ui_frame_context_t *ctx)
{
    if (!ctx) {
        return;
    }

    ui_status_update_header_track(ctx);
    ui_status_update_transport(ctx);
    ui_status_update_pitch_bpm(ctx);
    ui_status_update_time(ctx);
#ifndef WIN32
    ui_status_update_legacy_leds(ctx);
#endif
}

#endif
