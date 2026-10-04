#include "ui_dj_bridge.h"
#include "ui_artwork.h"
#include "ui_overview_wave_cache.h"
#include "ui_overview_renderer.h"
#include "ui_waveform_model.h"
#include "ui_color_preview.h"
#include "hot_cue_store.h"
#include "ui_beat_fx_format.h"
#include "ui_position_interpolator.h"
#include "ui_overview_window.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

#define RGB(r,g,b) ((uint16_t)((((r)>>3)<<11) | (((g)>>2)<<5) | ((b)>>3)))
/* Same index meanings as the shared waveform renderer. */
static const uint16_t palette[] = {
    RGB(0,0,0), RGB(255,46,110), RGB(58,123,255), RGB(38,224,255),
    RGB(255,255,255), RGB(56,245,140), RGB(255,183,51), RGB(181,124,255),
    RGB(90,93,100), RGB(255,23,68), RGB(107,63,0), RGB(0,230,118),
    RGB(0,229,255), RGB(255,171,0), RGB(224,64,251), RGB(255,214,0),
    RGB(255,23,68), RGB(124,77,255), RGB(41,121,255)
};

typedef struct {
    uint16_t *zoom_pixels, *mini_pixels;
    uint8_t *mini_indices;
    lv_image_dsc_t zoom, mini;
    ui_overview_wave_cache_t cache;
    uint32_t snapshot_version, track_generation, duration;
    uint16_t bpm;
    int zoom_width, zoom_height, mini_width, mini_height;
    bool metadata_valid;
    bool empty_shown;
    uint32_t cue_revision;
    uint8_t cue_count, loop_count;
    anlz_metadata_t render_meta;
    ui_position_interpolator_t position;
} bridge_deck_t;
static bridge_deck_t decks[DJ_DECKS];
static deck_core_beat_fx_state_t last_fx;
static uint16_t last_fx_time;
static bool fx_valid;
static uint8_t zoom_step;

void ui_dj_bridge_zoom_delta(int delta)
{
    zoom_step = ui_overview_zoom_apply_delta(zoom_step, delta);
}

void ui_dj_bridge_get_stats(uint8_t d, ui_dj_bridge_stats_t *out)
{
    if (!out) return;
    *out = (ui_dj_bridge_stats_t){0};
    if (d >= DJ_DECKS) return;
    const bridge_deck_t *s = &decks[d];
    if (s->zoom_pixels) out->surface_bytes += s->zoom.data_size;
    if (s->mini_pixels) out->surface_bytes += s->mini.data_size;
    if (s->mini_indices) out->surface_bytes += (uint32_t)s->mini_width * s->mini_height;
    out->columns_rendered = s->cache.stats.total_columns_rendered;
    out->cache_full_updates = s->cache.stats.update_count[UI_OVERVIEW_WAVE_CACHE_FULL];
}

static void *pixel_alloc(size_t bytes)
{
#ifdef ESP_PLATFORM
    return heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return calloc(1, bytes);
#endif
}

static void image_init(lv_image_dsc_t *image, void *pixels, int w, int h)
{
    *image = (lv_image_dsc_t){0};
    image->header.magic = LV_IMAGE_HEADER_MAGIC;
    image->header.cf = LV_COLOR_FORMAT_RGB565;
    image->header.w = (uint32_t)w;
    image->header.h = (uint32_t)h;
    image->header.stride = (uint32_t)w * 2u;
    image->data_size = (uint32_t)w * (uint32_t)h * 2u;
    image->data = pixels;
}

void ui_dj_bridge_reset(void)
{
    for (uint8_t d = 0; d < DJ_DECKS; ++d) {
        /* Unbind descriptors before freeing their pixel storage. */
        dj_ui_wave_set_strip(d, NULL, 0, 0);
        dj_ui_wave_set_image(d, DJ_WAVE_MINI, NULL, 0);
        free(decks[d].zoom_pixels);
        free(decks[d].mini_pixels);
        free(decks[d].mini_indices);
    }
    memset(decks, 0, sizeof decks);
    fx_valid = false;
}

void ui_dj_bridge_init(lv_obj_t *parent, const dj_ui_callbacks_t *callbacks,
                       bool ethernet, bool recorder)
{
    if (decks[0].zoom_pixels || decks[1].zoom_pixels)
        ui_dj_bridge_reset();
    dj_ui_create(parent);
    dj_ui_set_callbacks(callbacks);
    dj_ui_set_features(ethernet, recorder);
    zoom_step = ui_overview_zoom_step_default();
    lv_obj_update_layout(parent);
    for (uint8_t d = 0; d < DJ_DECKS; ++d) {
        bridge_deck_t *s = &decks[d];
        lv_area_t zoom, mini;
        if (!dj_ui_wave_get_area(d, DJ_WAVE_ZOOM, &zoom) ||
            !dj_ui_wave_get_area(d, DJ_WAVE_MINI, &mini)) continue;
        s->zoom_width = lv_area_get_width(&zoom);
        s->zoom_height = lv_area_get_height(&zoom);
        s->mini_width = lv_area_get_width(&mini);
        s->mini_height = lv_area_get_height(&mini);
        const int strip = s->zoom_width + 2 * UI_OVERVIEW_WAVE_CACHE_MARGIN_PX;
        const size_t mini_count = (size_t)s->mini_width * s->mini_height;
        s->zoom_pixels = pixel_alloc((size_t)strip * s->zoom_height * 2u);
        s->mini_pixels = pixel_alloc(mini_count * 2u);
        s->mini_indices = pixel_alloc(mini_count);
        if (s->zoom_pixels) {
            image_init(&s->zoom, s->zoom_pixels, strip, s->zoom_height);
            ui_overview_wave_cache_bind_strip(&s->cache, s->zoom_pixels,
                strip, strip, s->zoom_width, s->zoom_height,
                UI_OVERVIEW_WAVE_CACHE_MARGIN_PX, palette,
                sizeof palette / sizeof palette[0]);
            dj_ui_wave_set_source(d, DJ_WAVE_ZOOM, DJ_WAVE_SRC_IMAGE);
        }
        if (s->mini_pixels && s->mini_indices) {
            image_init(&s->mini, s->mini_pixels, s->mini_width, s->mini_height);
            dj_ui_wave_set_source(d, DJ_WAVE_MINI, DJ_WAVE_SRC_IMAGE);
            dj_ui_wave_set_image(d, DJ_WAVE_MINI, &s->mini, 0);
        }
    }
}

static void metadata_update(uint8_t d, bridge_deck_t *s,
                             const ui_frame_context_t *f,
                             const deck_loaded_track_summary_t *track)
{
    const anlz_metadata_t *meta = f->deck_meta[d];
    const ui_deck_track_info_t *info = f->deck_info[d];
    dj_ui_set_track(d, info && info->valid ? info->title : "No Track",
        info && info->valid ? info->artist : "", track->valid ? "USB" : "",
        0, 0, f->deck_duration_ms[d]);
    dj_ui_set_bpm(d, f->deck_bpm[d],
        meta && meta->beats && meta->beat_count ? meta->beats[0].time_ms : 0);
    dj_ui_set_key(d, info && info->valid && info->key[0] ? info->key : "--");
    uint32_t memory[DJ_MEMORY_CUES];
    uint8_t n = meta ? meta->memory_cue_count : 0;
    if (n > DJ_MEMORY_CUES) n = DJ_MEMORY_CUES;
    for (uint8_t i = 0; i < n; ++i) memory[i] = meta->memory_cues[i].start_ms;
    dj_ui_set_memory_cues(d, memory, n);
    s->metadata_valid = true;
}

static void cue_update(uint8_t d, const anlz_metadata_t *meta,
                        const deck_loaded_track_summary_t *track)
{
    hot_cue_store_blob_t source = {0}, local = {0}, effective = {0};
    if (meta) for (uint8_t i = 0; i < meta->cue_count && i < ANLZ_MAX_CUES; ++i) {
        const anlz_cue_t *cue = &meta->cues[i];
        if (cue->index >= DJ_HOTCUES ||
            (cue->type != ANLZ_CUE_SINGLE && cue->type != ANLZ_CUE_LOOP)) continue;
        source.valid_mask |= 1u << cue->index;
        source.slots[cue->index] = (hot_cue_store_slot_t){
            .pos_ms=cue->start_ms, .end_ms=cue->end_ms,
            .type=cue->type == ANLZ_CUE_LOOP ? HOT_CUE_STORE_TYPE_LOOP : HOT_CUE_STORE_TYPE_SINGLE};
    }
    esp_err_t result = track->valid && track->persistent_id.valid
        ? hot_cue_store_load(&track->persistent_id, &local) : ESP_ERR_NOT_FOUND;
    /* Match the existing performance view's fail-closed edit-store behavior. */
    if (result != ESP_OK && result != ESP_ERR_NOT_FOUND) source.valid_mask = 0;
    hot_cue_store_merge(&source, result == ESP_OK ? &local : NULL, &effective);
    decks[d].cue_count = decks[d].loop_count = 0;
    for (uint8_t i = 0; i < DJ_HOTCUES; ++i) {
        bool valid = (effective.valid_mask & (1u << i)) != 0;
        dj_ui_set_hotcue(d, i, valid, effective.slots[i].pos_ms, i);
        dj_ui_set_hotcue_loop(d, i, valid && effective.slots[i].type == HOT_CUE_STORE_TYPE_LOOP);
        if (valid) ++decks[d].cue_count;
        if (valid && effective.slots[i].type == HOT_CUE_STORE_TYPE_LOOP) ++decks[d].loop_count;
    }
}

void ui_dj_bridge_update(const ui_frame_context_t *f)
{
    if (!f) return;
    for (uint8_t d = 0; d < DJ_DECKS; ++d) {
        bridge_deck_t *s = &decks[d];
        deck_loaded_track_summary_t track = {0};
        deck_core_get_loaded_track(d, &track);
        if (!track.valid || !f->deck_duration_ms[d]) {
            /* A stale leased analysis snapshot may outlive UNLOAD. Never
             * republish its pixels or cues for an empty deck. */
            if (!s->empty_shown) {
                dj_ui_cancel_track_holds(d);
                dj_ui_set_track(d, "No Track", "", "", 0, 0, 0);
                dj_ui_set_bpm(d, 0, 0);
                dj_ui_set_key(d, "--");
                dj_ui_set_position(d, 0);
                dj_ui_set_transport(d, false, true);
                dj_ui_set_tempo(d, 0);
                dj_ui_set_master_tempo(d, false);
                dj_ui_set_sync(d, DJ_SYNC_OFF);
                dj_ui_set_vu(d, 0);
                dj_ui_set_beat(d, false, 0, false);
                dj_ui_set_cue_point(d, false, 0);
                dj_ui_set_loop(d, false, 0, 0);
                dj_ui_set_loop_armed(d, false, 0);
                dj_ui_set_memory_cues(d, NULL, 0);
                for (uint8_t i = 0; i < DJ_HOTCUES; ++i) {
                    dj_ui_set_hotcue(d, i, false, 0, i);
                    dj_ui_set_hotcue_loop(d, i, false);
                }
                dj_ui_wave_set_strip(d, NULL, 0, 0);
                dj_ui_wave_set_image(d, DJ_WAVE_MINI, NULL, 0);
                dj_ui_set_artwork_pixels(d, NULL);
                if (f->active_tab != DJ_TAB_LIBRARY)
                    dj_ui_library_set_load_locked(d, !deck_core_load_allowed(d));
                s->metadata_valid = false;
                s->cue_count = s->loop_count = 0;
                s->cache.valid = false;
                ui_position_interpolator_init(&s->position);
                s->empty_shown = true;
            }
            continue;
        }
        s->empty_shown = false;
        uint32_t version = anlz_snapshot_version(f->deck_anlz[d]);
        bool changed = !s->metadata_valid || s->snapshot_version != version ||
            s->track_generation != track.generation ||
            s->duration != f->deck_duration_ms[d] || s->bpm != f->deck_bpm[d];
        if (changed) {
            if (!s->metadata_valid || s->track_generation != track.generation) {
                dj_ui_cancel_track_holds(d);
                ui_position_interpolator_init(&s->position);
            }
            metadata_update(d, s, f, &track);
            s->cache.valid = false;
            s->snapshot_version = version;
            s->track_generation = track.generation;
            s->duration = f->deck_duration_ms[d]; s->bpm = f->deck_bpm[d];
            ui_waveform_source_t source = ui_waveform_source_select_for_overview_redraw(
                f->deck_meta[d], f->overview_wave_source[d].waveform_low,
                f->overview_wave_source[d].has_waveform);
            if (s->mini_indices && s->mini_pixels) {
                if (!ui_color_preview_draw(s->mini_indices, s->mini_width,
                    s->mini_width, s->mini_height, f->deck_meta[d],
                    s->duration, f->deck_analysis_span_ms[d]))
                    ui_overview_renderer_draw_mini_spans(s->mini_indices,
                    s->mini_width, s->mini_width, s->mini_height, &source,
                    s->duration, f->deck_analysis_span_ms[d]);
                for (int i = 0; i < s->mini_width * s->mini_height; ++i) {
                    uint8_t index = s->mini_indices[i];
                    s->mini_pixels[i] = index < sizeof palette / sizeof palette[0]
                        ? palette[index] : 0;
                }
                dj_ui_wave_invalidate(d, DJ_WAVE_MINI);
                dj_ui_wave_set_image(d, DJ_WAVE_MINI, &s->mini, 0);
            }
        }
        uint32_t revision = deck_core_hot_cue_revision();
        if (changed || s->cue_revision != revision) {
            cue_update(d, f->deck_meta[d], &track);
            s->cue_revision = revision;
        }
        const deck_state_t *state = &f->deck_state[d];
        uint32_t speed = f->mixer_snapshot.scratch_position_authoritative[d] ? 0u :
            (f->mixer_snapshot.effective_speed_permille[d] ?
             f->mixer_snapshot.effective_speed_permille[d] : f->deck_speed_permille[d]);
        uint32_t position = f->deck_duration_ms[d] ? ui_position_interpolator_update(
            &s->position, state->position_ms, f->deck_duration_ms[d],
            state->playing, speed, f->now_us) : 0;
        dj_ui_set_tempo(d, deck_core_pitch_percent(state));
        dj_ui_set_position(d, position);
        dj_ui_set_transport(d, state->playing, state->cue_held || !state->playing);
        dj_ui_set_master_tempo(d, state->master_tempo);
        uint32_t peak = (uint32_t)f->mixer_snapshot.deck_peak_display[d] * 255u / 32768u;
        dj_ui_set_vu(d, (uint8_t)(peak > 255u ? 255u : peak));
        dj_ui_set_sync(d, state->sync_enabled ? DJ_SYNC_LOCAL : DJ_SYNC_OFF);
        dj_ui_set_cue_point(d, track.valid, state->cue_point_ms);
        if (f->active_tab != DJ_TAB_LIBRARY)
            dj_ui_library_set_load_locked(d, !deck_core_load_allowed(d));
        const anlz_metadata_t *meta = f->deck_meta[d];
        ui_beat_indicator_state_t beat = ui_beat_indicator_calculate(
            position, meta ? meta->beats : NULL,
            meta ? meta->beat_count : 0, f->deck_bpm[d]);
        dj_ui_set_beat(d, beat.valid, beat.phase, beat.downbeat);
        deck_core_loop_display_t loop = deck_core_get_loop_display(d);
        dj_ui_set_loop(d, loop.active, loop.start_ms, loop.end_ms);
        dj_ui_set_loop_armed(d, loop.armed, loop.start_ms);
        if (f->active_tab == DJ_TAB_OVERVIEW && s->zoom_pixels) {
            uint16_t bpm_x100 = meta && meta->beats && meta->beat_count
                ? meta->beats[0].bpm_x100 : 0;
            dj_ui_wave_set_window_ms(d, ui_overview_window_ms_from_bpm_x100_for_zoom(
                bpm_x100, f->deck_bpm[d], zoom_step));
            ui_waveform_source_t source = ui_waveform_source_select_for_overview_redraw(
                meta, f->overview_wave_source[d].waveform_low,
                f->overview_wave_source[d].has_waveform);
            ui_overview_wave_cache_report_t report;
            ui_overview_wave_cache_set_loop(&s->cache, loop.active,
                                            loop.start_ms, loop.end_ms);
            const anlz_metadata_t *render_meta = NULL;
            if (meta) {
                s->render_meta = *meta;
                /* dj_ui overlays the effective edit bank. Source cues must
                 * not be burned into pixels after a local deletion. */
                s->render_meta.cue_count = 0;
                s->render_meta.memory_cue_count = 0;
                render_meta = &s->render_meta;
            }
            if (ui_overview_wave_cache_update(&s->cache, &source,
                f->deck_analysis_span_ms[d], render_meta, position,
                dj_ui_wave_get_window_ms(d), &report)) {
                dj_ui_wave_set_strip(d, &s->zoom, s->cache.view_origin_px,
                                     s->cache.center_ms);
            }
            /* Cache keeps pointer values to detect changes, but must never
             * dereference them outside this leased frame. */
        }
        const uint16_t *art = track.valid
            ? ui_artwork_get(track.track_key, UI_ARTWORK_DECK) : NULL;
        dj_ui_set_artwork_pixels(d, art);
    }
    dj_ui_set_target(f->active_deck);
    char cue_text[48];
    uint8_t active = f->active_deck < DJ_DECKS ? f->active_deck : 0;
    snprintf(cue_text, sizeof cue_text, "CUES: %u LOOPS: %u",
             decks[active].cue_count, decks[active].loop_count);
    dj_ui_set_field(DJ_F_HC_LOOPS, cue_text, DJ_TONE_NORMAL);
    snprintf(cue_text, sizeof cue_text, "D%u JOG: %s", (unsigned)active + 1u,
             f->deck_state[active].jog_cdj_mode ? "CDJ" : "VINYL");
    dj_ui_set_field(DJ_F_HC_TARGET, cue_text, DJ_TONE_NORMAL);
    const anlz_metadata_t *active_meta = decks[active].metadata_valid ? f->deck_meta[active] : NULL;
    snprintf(cue_text, sizeof cue_text, "MEMORY: %u%s",
             active_meta ? active_meta->memory_cue_count : 0u,
             active_meta && active_meta->memory_cues_truncated ? " (TRUNCATED)" : "");
    dj_ui_set_field(DJ_F_HC_MEMORY, cue_text,
                    active_meta && active_meta->memory_cues_truncated ? DJ_TONE_WARN : DJ_TONE_MUTED);
    ui_beat_fx_overview_text_t fx_text;
    ui_beat_fx_format_overview(&f->beat_fx_state, &fx_text);
    uint8_t target = f->beat_fx_state.target == CTRL_BEAT_FX_TARGET_BOTH ? 0 :
                    (uint8_t)f->beat_fx_state.target + 1u;
    uint8_t tempo_deck = target ? target - 1u : f->active_deck;
    static const float lengths[] = {0.25f,0.5f,1.f,2.f,4.f};
    float tempo = f->deck_bpm[tempo_deck] * (1.f +
        deck_core_pitch_percent(&f->deck_state[tempo_deck]) / 100.f);
    float milliseconds = tempo > 0.f && f->beat_fx_state.beat < DECK_CORE_BEAT_FX_BEAT_COUNT
        ? 60000.f / tempo * lengths[f->beat_fx_state.beat] : 0.f;
    uint16_t time = (uint16_t)(milliseconds > 65535.f ? 65535.f : milliseconds);
    const deck_core_beat_fx_state_t *fx = &f->beat_fx_state;
    if (!fx_valid || fx->effect != last_fx.effect || fx->target != last_fx.target ||
        fx->beat != last_fx.beat || fx->depth != last_fx.depth ||
        fx->enabled != last_fx.enabled || time != last_fx_time) {
        dj_ui_set_fx(fx_text.effect, target, f->beat_fx_state.beat, time,
        (uint8_t)((uint32_t)f->beat_fx_state.depth * 100u / 127u),
        f->beat_fx_state.enabled);
        last_fx = *fx; last_fx_time = time; fx_valid = true;
    }
    dj_ui_set_field(DJ_F_LOAD_LOCK, deck_core_get_load_lock()
        ? "LOAD LOCK: ON" : "LOAD LOCK: OFF", DJ_TONE_NORMAL);
}
