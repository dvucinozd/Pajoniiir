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
    snprintf(cache->text, sizeof(cache->text), "%s", safe_text);
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
    if (strcmp(status, "HOST BUSY") == 0) {
        return COL_AMBER;
    }
    if (strcmp(status, "JOIN OFFLINE") == 0 ||
        strcmp(status, "JOIN FAILED") == 0 ||
        strcmp(status, "MANIFEST ERR") == 0 ||
        strcmp(status, "DAT ERR") == 0 ||
        strcmp(status, "AUDIO ERR") == 0 ||
        strcmp(status, "TASK CREATE ERR") == 0 ||
        strcmp(status, "STOP ERR") == 0 ||
        strcmp(status, "NO MEM") == 0 ||
        strcmp(status, "NO AUDIO FRAME") == 0 ||
        strcmp(status, "CODEC OPEN ERR") == 0 ||
        strcmp(status, "NOT FOUND") == 0 ||
        strcmp(status, "LOAD ERR") == 0) {
        return COL_RED;
    }
    if (strcmp(status, "JOINED") == 0 ||
        strcmp(status, "CACHE READY") == 0 ||
        strcmp(status, "TRACK LOADED") == 0) {
        return COL_GREEN;
    }
    if (strcmp(status, "LOADING") == 0 ||
        strcmp(status, "CACHE START") == 0 ||
        strcmp(status, "MANIFEST") == 0 ||
        strcmp(status, "ANLZ0000.DAT") == 0 ||
        strcmp(status, "ANLZ0000.EXT") == 0 ||
        strcmp(status, "audio.mp3") == 0) {
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
    return audio_engine_raw_pitch_to_percent(state->pitch);
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
        lv_label_set_text_fmt(s_widgets.time_elapsed, "%02u:%02u.%02u",
                              (unsigned)(elapsed_ms / 60000u),
                              (unsigned)((elapsed_ms % 60000u) / 1000u),
                              (unsigned)((elapsed_ms % 1000u) / 10u));
    }
    if (remain_seconds != s_cache_remain_seconds) {
        s_cache_remain_seconds = remain_seconds;
        lv_label_set_text_fmt(s_widgets.time_remain, "-%02u:%02u.%02u",
                              (unsigned)(remain_ms / 60000u),
                              (unsigned)((remain_ms % 60000u) / 1000u),
                              (unsigned)((remain_ms % 1000u) / 10u));
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
    } else if (!ui_status_has_override()) {
        ui_status_set_indicator(transport.text,
                                transport.kind == UI_STATUS_TRANSPORT_PLAY ? COL_GREEN : COL_AMBER);
    }
}

#ifndef WIN32
static void ui_status_update_legacy_leds(const ui_frame_context_t *ctx)
{
    static uint8_t s_last_led_state[LED_COUNT] = {0xFF, 0xFF, 0xFF, 0xFF};
    static uint8_t s_last_led_deck = CTRL_DECK_NONE;
    if (s_last_led_deck != ctx->active_deck) {
        memset(s_last_led_state, 0xFF, sizeof(s_last_led_state));
        s_last_led_deck = ctx->active_deck;
    }

    ui_active_deck_leds_t leds =
        ui_active_deck_leds_calculate(ctx->active_state.playing,
                                      ctx->active_state.position_ms,
                                      ctx->active_state.cue_point_ms,
                                      ctx->active_duration_ms,
                                      ctx->active_beat_state_valid,
                                      ctx->active_beat_state.progress_permille);
    const uint8_t next_leds[LED_COUNT] = {
        [LED_CUE] = leds.cue,
        [LED_PLAY] = leds.play,
        [LED_BEAT] = leds.beat,
        [LED_END] = leds.end,
    };
    for (int led = 0; led < LED_COUNT; led++) {
        if (next_leds[led] != s_last_led_state[led]) {
            s_last_led_state[led] = next_leds[led];
            control_link_send_led((led_id_t)led, next_leds[led]);
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
