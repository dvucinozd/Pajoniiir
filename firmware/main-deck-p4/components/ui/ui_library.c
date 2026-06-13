#include "ui_library.h"
#include "ui_diagnostics.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static void ui_library_truncate_str(char *dest, size_t dest_size, const char *src, size_t max_len)
{
    if (!dest || dest_size == 0) {
        return;
    }
    if (!src) {
        src = "";
    }
    if (max_len == 0 || dest_size < 4) {
        dest[0] = '\0';
        return;
    }

    size_t limit = max_len;
    if (limit >= dest_size) {
        limit = dest_size - 1;
    }

    size_t len = strlen(src);
    if (len <= limit) {
        snprintf(dest, dest_size, "%s", src);
        return;
    }

    size_t copy_len = limit > 3 ? limit - 3 : 0;
    if (copy_len >= dest_size) {
        copy_len = dest_size - 1;
    }
    memcpy(dest, src, copy_len);
    dest[copy_len] = '\0';
    strncat(dest, "...", dest_size - strlen(dest) - 1);
}

void ui_library_format_row_text(ui_library_row_text_t *out,
                                const char *title,
                                const char *artist,
                                uint16_t bpm,
                                uint32_t duration_ms)
{
    if (!out) {
        return;
    }

    ui_library_truncate_str(out->title, sizeof(out->title), title, 26);
    ui_library_truncate_str(out->artist, sizeof(out->artist), artist, 18);

    uint32_t secs = duration_ms / 1000u;
    snprintf(out->bpm, sizeof(out->bpm), "%u", (unsigned)bpm);
    snprintf(out->duration, sizeof(out->duration), "%u:%02u",
             (unsigned)(secs / 60u),
             (unsigned)(secs % 60u));
}

ui_library_update_plan_t ui_library_plan_update(int active_tab,
                                                bool needs_refresh,
                                                bool usb_removed_pending)
{
    return (ui_library_update_plan_t){
        .apply_usb_removed = usb_removed_pending,
        .poll_track_load_result = true,
        .refresh_library = needs_refresh,
        .focus_library_table = active_tab == 1,
    };
}

#ifndef UI_LIBRARY_HOST_TEST

#include "library.h"
#include "esp_log.h"
#include "ui_lvgl_backend.h"
#include "ui_theme.h"

#ifndef WIN32
#include "audio_engine.h"
#include "cdj_link_client.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "media_catalog.h"
#include "remote_cache.h"

#define UI_TRACK_LOAD_STACK (10 * 1024)
#endif

static const char *TAG = "ui_library";

static ui_library_config_t s_library_config;
static lv_obj_t *s_library_screen = NULL;
static lv_obj_t *s_library_table = NULL;
static lv_obj_t *s_label_library_source = NULL;
static lv_obj_t *s_btn_library_load = NULL;
static lv_obj_t *s_btn_library_load_deck2 = NULL;
static lv_obj_t *s_label_library_hint = NULL;
static int s_active_tab = 0;
static int s_selected_track_idx = 0;
static volatile bool s_library_needs_refresh = false;
static bool s_sort_artist_desc = false;
static bool s_sort_name_desc = false;
static bool s_sort_bpm_desc = false;
static bool s_track_load_busy = false;
static uint8_t s_library_load_request_deck = CTRL_DECK_1;

#ifndef WIN32
static media_loaded_track_t s_loaded_media[DECK_CORE_DECK_COUNT];
static bool s_loaded_media_valid[DECK_CORE_DECK_COUNT];
static media_source_t s_loaded_media_source[DECK_CORE_DECK_COUNT] = {
    MEDIA_SOURCE_LOCAL_USB,
    MEDIA_SOURCE_LOCAL_USB,
};
static QueueHandle_t s_track_load_result_q = NULL;
static volatile bool s_usb_removed_pending = false;

typedef struct {
    int index;
    uint8_t deck;
    uint32_t generation;
    media_source_t source;
    media_catalog_track_t item;
    media_loaded_track_t loaded;
    esp_err_t rc;
    char status[40];
} ui_track_load_result_t;

typedef struct {
    int index;
    uint8_t deck;
    uint32_t generation;
    media_source_t source;
} ui_track_load_request_t;

static ui_track_load_result_t s_track_load_worker_result;
#endif

#define s_style_screen_bg (*s_library_config.styles.screen_bg)
#define s_style_btn_primary (*s_library_config.styles.btn_primary)
#define s_style_btn_secondary (*s_library_config.styles.btn_secondary)
#define s_style_btn_disabled (*s_library_config.styles.btn_disabled)
#define s_style_pressed (*s_library_config.styles.pressed)

static uint8_t ui_library_deck_index(uint8_t deck)
{
    return deck < DECK_CORE_DECK_COUNT ? deck : DECK_CORE_COMPAT_DECK;
}

static int ui_library_media_count(void)
{
#ifndef WIN32
    return media_catalog_count();
#else
    return library_count();
#endif
}

static void ui_library_status_hold(const char *text, lv_color_t color, uint32_t hold_ms)
{
    if (s_library_config.actions.status_hold) {
        s_library_config.actions.status_hold(text, color, hold_ms);
    }
}

static lv_color_t ui_library_status_color_for_text(const char *text)
{
    if (s_library_config.actions.status_color_for_text) {
        return s_library_config.actions.status_color_for_text(text);
    }
    return COL_TEXT_DIM;
}

static void ui_library_cache_invalidate(void)
{
    if (s_library_config.actions.cache_invalidate) {
        s_library_config.actions.cache_invalidate();
    }
}

static void ui_library_set_header_track(const char *title, const char *artist, uint16_t bpm)
{
    if (s_library_config.actions.set_header_track) {
        s_library_config.actions.set_header_track(title, artist, bpm);
    }
}

static void ui_library_set_load_busy(bool busy, const char *hint)
{
    lv_obj_t *buttons[] = {s_btn_library_load, s_btn_library_load_deck2};
    for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
        lv_obj_t *btn = buttons[i];
        if (!btn) continue;
        if (busy) {
            lv_obj_add_state(btn, LV_STATE_DISABLED);
            lv_obj_add_style(btn, &s_style_btn_disabled, LV_PART_MAIN);
        } else {
            lv_obj_clear_state(btn, LV_STATE_DISABLED);
            lv_obj_remove_style(btn, &s_style_btn_disabled, LV_PART_MAIN);
        }
    }

    if (s_label_library_hint) {
        lv_label_set_text(s_label_library_hint, hint ? hint : "SELECT TRACK\nLOAD D1/D2");
    }
}

static void ui_library_update_source_label(void)
{
    if (!s_label_library_source) {
        return;
    }
#ifndef WIN32
    if (media_catalog_get_source() == MEDIA_SOURCE_REMOTE_LINK) {
        lv_label_set_text_fmt(s_label_library_source, "JOINED  %d TRACKS", media_catalog_count());
    } else {
        lv_label_set_text_fmt(s_label_library_source, "LOCAL USB  %d TRACKS", media_catalog_count());
    }
#else
    lv_label_set_text_fmt(s_label_library_source, "LOCAL USB  %d TRACKS", library_count());
#endif
}

static void ui_library_fill_row(int i)
{
    const char *title = NULL;
    const char *artist = NULL;
    uint16_t bpm = 0;
    uint32_t duration_ms = 0;

#ifndef WIN32
    media_catalog_row_t row;
    if (media_catalog_get_row(i, &row) != ESP_OK) {
        return;
    }
    title = row.title;
    artist = row.artist;
    bpm = row.bpm;
    duration_ms = row.duration_ms;
#else
    const library_track_t *track = library_get_ptr(i);
    if (!track) {
        return;
    }
    title = track->title;
    artist = track->artist;
    bpm = track->bpm;
    duration_ms = track->duration_ms;
#endif

    ui_library_row_text_t text;
    ui_library_format_row_text(&text, title, artist, bpm, duration_ms);
    lv_table_set_cell_value(s_library_table, i + 1, 0, text.title);
    lv_table_set_cell_value(s_library_table, i + 1, 1, text.artist);
    lv_table_set_cell_value(s_library_table, i + 1, 2, text.bpm);
    lv_table_set_cell_value(s_library_table, i + 1, 3, text.duration);
}

static void ui_library_populate_rows(void)
{
    if (!s_library_table) {
        return;
    }
    int n_tracks = ui_library_media_count();
    lv_table_set_row_count(s_library_table, n_tracks + 1);
    for (int i = 0; i < n_tracks; i++) {
        ui_library_fill_row(i);
    }
    ui_library_update_source_label();
}

static void ui_library_apply_loaded_track(uint8_t deck,
                                          const char *title,
                                          const char *artist,
                                          uint16_t bpm,
                                          uint32_t duration_ms,
                                          const uint8_t waveform_low[400],
                                          bool has_waveform,
                                          const anlz_metadata_t *meta)
{
    deck = ui_library_deck_index(deck);
    if (s_library_config.actions.set_deck_track_info) {
        s_library_config.actions.set_deck_track_info(deck, title, artist, bpm, duration_ms);
    }
    if (s_library_config.actions.set_deck_anlz) {
        s_library_config.actions.set_deck_anlz(deck, meta);
    }
    if (s_library_config.actions.load_waveform_data) {
        s_library_config.actions.load_waveform_data(deck, duration_ms, waveform_low, has_waveform, meta);
    }
    if (s_library_config.actions.set_loop_shadow) {
        s_library_config.actions.set_loop_shadow(deck, false, 0, 0, 0);
    }
    if (deck == CTRL_DECK_1) {
        ui_library_cache_invalidate();
        ui_library_set_header_track(title, artist, bpm);
    }
    if (s_library_config.actions.is_performance_target_active &&
        s_library_config.actions.is_performance_target_active(deck) &&
        s_library_config.actions.update_hot_cues) {
        s_library_config.actions.update_hot_cues();
    }
}

#ifndef WIN32
static void ui_track_load_worker(void *arg)
{
    ui_track_load_request_t req = *(ui_track_load_request_t *)arg;
    free(arg);

    ui_track_load_result_t *result = &s_track_load_worker_result;
    memset(result, 0, sizeof(*result));
    result->index = req.index;
    result->deck = req.deck;
    result->generation = req.generation;
    result->source = req.source;
    result->rc = ESP_OK;

    if (media_catalog_get(req.index, &result->item) != ESP_OK) {
        result->rc = ESP_ERR_NOT_FOUND;
        snprintf(result->status, sizeof(result->status), "NO TRACK");
    } else {
        result->rc = media_catalog_load(req.index, &result->loaded);
        if (result->rc != ESP_OK) {
            const char *status = (req.source == MEDIA_SOURCE_REMOTE_LINK) ? remote_cache_status() : "LOAD ERR";
            snprintf(result->status, sizeof(result->status), "%s", status && status[0] ? status : "LOAD ERR");
        } else {
            if (req.deck == CTRL_DECK_1) {
                audio_engine_clear_loop();
            }
            deck_core_reset_deck(req.deck);
            result->rc = audio_engine_deck_load(req.deck,
                                                result->loaded.audio_path,
                                                result->loaded.has_pvbr ? result->loaded.pvbr : NULL,
                                                result->loaded.duration_ms);
            if (result->rc != ESP_OK) {
                const char *audio_err = audio_engine_last_error_text();
                snprintf(result->status, sizeof(result->status), "%s",
                         audio_err && audio_err[0] ? audio_err : "AUDIO ERR");
            } else {
                snprintf(result->status, sizeof(result->status), "TRACK LOADED");
            }
        }
    }

    if (s_track_load_result_q) {
        xQueueOverwrite(s_track_load_result_q, result);
    }
    if (ui_diagnostics_enabled()) {
        ESP_LOGI(TAG, "ui_load stack high water=%u words",
                 (unsigned)uxTaskGetStackHighWaterMark(NULL));
    }
    vTaskDelete(NULL);
}

static void ui_submit_track_load(int index, uint8_t deck)
{
    if (!s_track_load_result_q) {
        s_track_load_result_q = xQueueCreate(1, sizeof(ui_track_load_result_t));
    }
    if (!s_track_load_result_q) {
        ui_library_status_hold("NO QUEUE", COL_RED, 2500);
        ui_library_set_load_busy(false, "NO QUEUE");
        s_track_load_busy = false;
        return;
    }

    ui_track_load_result_t stale;
    while (xQueueReceive(s_track_load_result_q, &stale, 0) == pdTRUE) {
    }

    ui_track_load_request_t *req = malloc(sizeof(*req));
    if (!req) {
        ui_library_status_hold("NO MEM", COL_RED, 2500);
        ui_library_set_load_busy(false, "NO MEM");
        s_track_load_busy = false;
        return;
    }
    req->index = index;
    req->deck = deck;
    req->generation = library_generation();
    req->source = media_catalog_get_source();

    if (xTaskCreate(ui_track_load_worker, "ui_load", UI_TRACK_LOAD_STACK, req, 3, NULL) != pdPASS) {
        free(req);
        ui_library_status_hold("NO TASK", COL_RED, 2500);
        ui_library_set_load_busy(false, "NO TASK");
        s_track_load_busy = false;
    }
}

static void ui_apply_usb_removed(void)
{
    s_usb_removed_pending = false;
    bool removed_loaded = false;
    for (uint8_t deck = 0; deck < DECK_CORE_DECK_COUNT; deck++) {
        if (s_loaded_media_valid[deck] && s_loaded_media_source[deck] == MEDIA_SOURCE_LOCAL_USB) {
            s_loaded_media_valid[deck] = false;
            if (s_library_config.actions.clear_deck_track_info) {
                s_library_config.actions.clear_deck_track_info(deck);
            }
            if (s_library_config.actions.set_deck_anlz) {
                s_library_config.actions.set_deck_anlz(deck, NULL);
            }
            if (s_library_config.actions.set_loop_shadow) {
                s_library_config.actions.set_loop_shadow(deck, false, 0, 0, 0);
            }
            if (s_library_config.actions.load_waveform_data) {
                s_library_config.actions.load_waveform_data(deck, 0, NULL, false, NULL);
            }
            removed_loaded = true;
        }
    }
    if (removed_loaded) {
        ui_library_cache_invalidate();
        ui_library_set_header_track("No Track", "", 0);
        if (s_library_config.actions.update_loop_screen_state) {
            s_library_config.actions.update_loop_screen_state();
        }
        if (s_library_config.actions.update_hot_cues) {
            s_library_config.actions.update_hot_cues();
        }
        ui_library_status_hold("USB REMOVED", COL_AMBER, 2500);
    }
    if (media_catalog_get_source() == MEDIA_SOURCE_LOCAL_USB) {
        ui_library_set_load_busy(false, "USB REMOVED");
        s_track_load_busy = false;
    }
}

static void ui_poll_track_load_result(void)
{
    if (!s_track_load_result_q) return;

    ui_track_load_result_t result;
    while (xQueueReceive(s_track_load_result_q, &result, 0) == pdTRUE) {
        bool stale = (result.source != media_catalog_get_source());
        if (result.source == MEDIA_SOURCE_LOCAL_USB &&
            result.generation != library_generation()) {
            stale = true;
        }
        if (stale) {
            s_track_load_busy = false;
            ui_library_set_load_busy(false,
                                     result.source == MEDIA_SOURCE_LOCAL_USB ? "USB REMOVED" : "STALE");
            continue;
        }

        if (result.rc != ESP_OK) {
            const char *display = result.status[0] ? result.status : "LOAD ERR";
            ESP_LOGW(TAG, "track load worker failed index=%d: %s", result.index, esp_err_to_name(result.rc));
            ui_library_status_hold(display, ui_library_status_color_for_text(display), 3500);
            ui_library_set_load_busy(false, display);
            s_track_load_busy = false;
            continue;
        }

        if (result.source == MEDIA_SOURCE_LOCAL_USB) {
            mock_library_load_track_to_deck(result.index);
        }
        uint8_t deck = ui_library_deck_index(result.deck);
        s_loaded_media[deck] = result.loaded;
        s_loaded_media_valid[deck] = true;
        s_loaded_media_source[deck] = result.source;
        const uint16_t bpm = result.loaded.bpm ? result.loaded.bpm : result.item.bpm;
        const anlz_metadata_t *meta = media_catalog_get_loaded_anlz_for_source(result.source);
        ui_library_apply_loaded_track(deck,
                                      result.item.title,
                                      result.item.artist,
                                      bpm,
                                      result.loaded.duration_ms,
                                      result.loaded.waveform_low,
                                      result.loaded.has_waveform != 0,
                                      meta);

        ESP_LOGI(TAG, "Audio: loaded deck %u: %s (autoplay off)",
                 (unsigned)result.deck + 1u, result.loaded.audio_path);
        const char *loaded_text = result.deck == CTRL_DECK_1 ? "D1 LOADED" : "D2 LOADED";
        ui_library_status_hold(loaded_text, COL_GREEN, 2000);
        ui_library_set_load_busy(false, loaded_text);
        s_track_load_busy = false;
    }
}
#endif

static void ui_library_load_selected_deck(uint8_t deck)
{
    if (s_track_load_busy) {
        ui_library_status_hold("LOAD BUSY", COL_AMBER, 1200);
        return;
    }
    s_track_load_busy = true;
    ui_library_set_load_busy(true, "LOAD BUSY");

#ifdef WIN32
    library_track_t *track = library_get_ptr(s_selected_track_idx);
    if (!track) {
        ui_library_set_load_busy(false, NULL);
        s_track_load_busy = false;
        return;
    }

    mock_library_load_track_to_deck(s_selected_track_idx);
    library_load_anlz(track);
    library_load_current_anlz(track);
    const anlz_metadata_t *meta = library_get_current_anlz();
    ui_library_apply_loaded_track(deck,
                                  track->title,
                                  track->artist,
                                  track->bpm,
                                  track->duration_ms,
                                  track->waveform_low,
                                  track->has_waveform != 0,
                                  meta);

    ESP_LOGI(TAG, "Loaded track to deck %u: %s by %s (waveform=%d)",
             (unsigned)deck + 1u, track->title, track->artist, track->has_waveform);
#else
    media_catalog_track_t item;
    if (media_catalog_get(s_selected_track_idx, &item) != ESP_OK) {
        ESP_LOGW(TAG, "No catalog row at index %d", s_selected_track_idx);
        ui_library_set_load_busy(false, NULL);
        s_track_load_busy = false;
        return;
    }

    const bool remote_source = (media_catalog_get_source() == MEDIA_SOURCE_REMOTE_LINK);
    ui_library_status_hold(remote_source ? "CACHE START" : "LOADING", COL_ACCENT, 1500);
    ui_submit_track_load(s_selected_track_idx, deck);
    return;
#endif
    ui_library_status_hold("TRACK LOADED", COL_GREEN, 2000);
    ui_library_set_load_busy(false, "TRACK LOADED");
    s_track_load_busy = false;
}

static void library_load_event_cb(lv_event_t *e)
{
    uint8_t deck = s_library_load_request_deck;
    if (e) {
        lv_obj_t *btn = lv_event_get_target(e);
        deck = (uint8_t)(uintptr_t)lv_obj_get_user_data(btn);
    }
    ui_library_load_selected_deck(deck);
}

static void ui_library_preserve_selection_by_key(uint32_t target_key)
{
    if (target_key == 0) {
        return;
    }
#ifndef WIN32
    int n = media_catalog_count();
    for (int i = 0; i < n; i++) {
        media_catalog_row_t t;
        if (media_catalog_get_row(i, &t) == ESP_OK && t.track_key == target_key) {
            s_selected_track_idx = i;
            break;
        }
    }
#else
    int n = library_count();
    for (int i = 0; i < n; i++) {
        library_track_t *t = library_get_ptr(i);
        if (t && t->track_id == target_key) {
            s_selected_track_idx = i;
            break;
        }
    }
#endif
}

static uint32_t ui_library_selected_key(void)
{
#ifndef WIN32
    media_catalog_row_t sel_track;
    return (media_catalog_get_row(s_selected_track_idx, &sel_track) == ESP_OK)
           ? sel_track.track_key
           : 0;
#else
    library_track_t *sel_track = library_get_ptr(s_selected_track_idx);
    return sel_track ? sel_track->track_id : 0;
#endif
}

static void library_sort_artist_event_cb(lv_event_t *e)
{
    (void)e;
    if (!s_library_table) return;
    uint32_t target_key = ui_library_selected_key();
    s_sort_artist_desc = !s_sort_artist_desc;
#ifndef WIN32
    media_catalog_sort(0, s_sort_artist_desc);
#else
    library_sort(0, s_sort_artist_desc);
#endif
    ui_refresh_library();
    ui_library_preserve_selection_by_key(target_key);
    lv_table_set_selected_cell(s_library_table, s_selected_track_idx + 1, 0);
}

static void library_sort_name_event_cb(lv_event_t *e)
{
    (void)e;
    if (!s_library_table) return;
    uint32_t target_key = ui_library_selected_key();
    s_sort_name_desc = !s_sort_name_desc;
#ifndef WIN32
    media_catalog_sort(1, s_sort_name_desc);
#else
    library_sort(1, s_sort_name_desc);
#endif
    ui_refresh_library();
    ui_library_preserve_selection_by_key(target_key);
    lv_table_set_selected_cell(s_library_table, s_selected_track_idx + 1, 0);
}

static void library_sort_bpm_event_cb(lv_event_t *e)
{
    (void)e;
    if (!s_library_table) return;
    uint32_t target_key = ui_library_selected_key();
    s_sort_bpm_desc = !s_sort_bpm_desc;
#ifndef WIN32
    media_catalog_sort(2, s_sort_bpm_desc);
#else
    library_sort(2, s_sort_bpm_desc);
#endif
    ui_refresh_library();
    ui_library_preserve_selection_by_key(target_key);
    lv_table_set_selected_cell(s_library_table, s_selected_track_idx + 1, 0);
}

static void library_table_event_cb(lv_event_t *e)
{
    lv_obj_t *table = lv_event_get_target(e);
    uint32_t row;
    uint32_t col;
    lv_table_get_selected_cell(table, &row, &col);

    if (row > 0 && (int)row <= ui_library_media_count()) {
        int new_idx = (int)row - 1;
        if (new_idx != s_selected_track_idx) {
            int old_idx = s_selected_track_idx;
            s_selected_track_idx = new_idx;
            ui_library_fill_row(old_idx);
            ui_library_fill_row(new_idx);
            lv_table_set_selected_cell(table, row, 0);
            ESP_LOGD(TAG, "Library selected track index: %d", s_selected_track_idx);
        }
    }
}

static void library_source_local_event_cb(lv_event_t *e)
{
    (void)e;
#ifndef WIN32
    media_catalog_set_source(MEDIA_SOURCE_LOCAL_USB);
#endif
    s_selected_track_idx = 0;
    ui_library_update_source_label();
    ui_refresh_library();
}

static void library_source_joined_event_cb(lv_event_t *e)
{
    (void)e;
#ifndef WIN32
    esp_err_t rc = cdj_link_client_start();
    if (rc == ESP_OK) {
        rc = media_catalog_refresh_remote();
    }
    if (rc == ESP_OK) {
        media_catalog_set_source(MEDIA_SOURCE_REMOTE_LINK);
        s_selected_track_idx = 0;
        ui_library_status_hold("JOINED", COL_GREEN, 2000);
    } else {
        ui_library_status_hold("JOIN FAILED", COL_RED, 3500);
        ESP_LOGW(TAG, "joined library refresh failed: %s", esp_err_to_name(rc));
    }
#endif
    ui_library_update_source_label();
    ui_refresh_library();
}

void ui_library_init(const ui_library_config_t *config)
{
    memset(&s_library_config, 0, sizeof(s_library_config));
    if (config) {
        s_library_config = *config;
    }
}

lv_obj_t *ui_library_create(lv_obj_t *parent)
{
    s_library_screen = lv_obj_create(parent);
    lv_obj_remove_style_all(s_library_screen);
    lv_obj_add_style(s_library_screen, &s_style_screen_bg, LV_PART_MAIN);
    lv_obj_set_size(s_library_screen, s_library_config.hor_res, s_library_config.content_h);
    lv_obj_set_pos(s_library_screen, 0, s_library_config.content_y);

    s_library_table = lv_table_create(s_library_screen);
    lv_obj_set_size(s_library_table, 600, 330);
    lv_obj_set_pos(s_library_table, 10, 10);
    lv_obj_add_event_cb(s_library_table, library_table_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_set_style_min_height(s_library_table, 36, LV_PART_ITEMS);
    lv_obj_set_style_max_height(s_library_table, 36, LV_PART_ITEMS);
    lv_obj_set_style_pad_left(s_library_table, 6, LV_PART_ITEMS);
    lv_obj_set_style_pad_right(s_library_table, 6, LV_PART_ITEMS);
    lv_obj_set_style_pad_top(s_library_table, 8, LV_PART_ITEMS);
    lv_obj_set_style_pad_bottom(s_library_table, 8, LV_PART_ITEMS);
    lv_obj_set_style_border_color(s_library_table, COL_BORDER, LV_PART_ITEMS);
    lv_obj_set_style_border_width(s_library_table, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_side(s_library_table, LV_BORDER_SIDE_BOTTOM, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(s_library_table, COL_TABLE_ROW, LV_PART_ITEMS);
    lv_obj_set_style_text_color(s_library_table, COL_TEXT_MUTED, LV_PART_ITEMS);
    lv_obj_set_style_text_font(s_library_table, &lv_font_montserrat_16, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(s_library_table, COL_TABLE_ALT, LV_PART_ITEMS | LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(s_library_table, COL_ACCENT, LV_PART_ITEMS | LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(s_library_table, 3, LV_PART_ITEMS | LV_STATE_FOCUSED);
    lv_obj_set_style_border_side(s_library_table, LV_BORDER_SIDE_BOTTOM, LV_PART_ITEMS | LV_STATE_FOCUSED);
    lv_obj_set_style_border_opa(s_library_table, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_FOCUSED);
    lv_obj_set_style_text_color(s_library_table, COL_ON_ACCENT, LV_PART_ITEMS | LV_STATE_FOCUSED);
    lv_table_set_column_width(s_library_table, 0, 290);
    lv_table_set_column_width(s_library_table, 1, 170);
    lv_table_set_column_width(s_library_table, 2, 60);
    lv_table_set_column_width(s_library_table, 3, 80);
    lv_table_set_cell_value(s_library_table, 0, 0, "TITLE");
    lv_table_set_cell_value(s_library_table, 0, 1, "ARTIST");
    lv_table_set_cell_value(s_library_table, 0, 2, "BPM");
    lv_table_set_cell_value(s_library_table, 0, 3, "TIME");
    ui_library_populate_rows();

    lv_obj_t *btn_src_local = lv_button_create(s_library_screen);
    lv_obj_remove_style_all(btn_src_local);
    lv_obj_add_style(btn_src_local, &s_style_btn_secondary, LV_PART_MAIN);
    lv_obj_add_style(btn_src_local, &s_style_pressed, LV_STATE_PRESSED);
    lv_obj_set_size(btn_src_local, 72, 38);
    lv_obj_set_pos(btn_src_local, 630, 10);
    lv_obj_add_event_cb(btn_src_local, library_source_local_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_remove_flag(btn_src_local, LV_OBJ_FLAG_CLICK_FOCUSABLE);

    lv_obj_t *lbl_src_local = lv_label_create(btn_src_local);
    lv_label_set_text(lbl_src_local, "LOCAL");
    lv_obj_set_style_text_font(lbl_src_local, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_src_local, COL_TEXT, LV_PART_MAIN);
    lv_obj_align(lbl_src_local, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *btn_src_join = lv_button_create(s_library_screen);
    lv_obj_remove_style_all(btn_src_join);
    lv_obj_add_style(btn_src_join, &s_style_btn_secondary, LV_PART_MAIN);
    lv_obj_add_style(btn_src_join, &s_style_pressed, LV_STATE_PRESSED);
    lv_obj_set_size(btn_src_join, 72, 38);
    lv_obj_set_pos(btn_src_join, 708, 10);
    lv_obj_add_event_cb(btn_src_join, library_source_joined_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_remove_flag(btn_src_join, LV_OBJ_FLAG_CLICK_FOCUSABLE);

    lv_obj_t *lbl_src_join = lv_label_create(btn_src_join);
    lv_label_set_text(lbl_src_join, "JOINED");
    lv_obj_set_style_text_font(lbl_src_join, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_src_join, COL_TEXT, LV_PART_MAIN);
    lv_obj_align(lbl_src_join, LV_ALIGN_CENTER, 0, 0);

    s_label_library_source = lv_label_create(s_library_screen);
    lv_obj_set_style_text_font(s_label_library_source, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_label_library_source, COL_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_pos(s_label_library_source, 630, 52);
    ui_library_update_source_label();

    s_btn_library_load = lv_button_create(s_library_screen);
    lv_obj_remove_style_all(s_btn_library_load);
    lv_obj_add_style(s_btn_library_load, &s_style_btn_primary, LV_PART_MAIN);
    lv_obj_add_style(s_btn_library_load, &s_style_pressed, LV_STATE_PRESSED);
    lv_obj_set_size(s_btn_library_load, 72, 50);
    lv_obj_set_pos(s_btn_library_load, 630, 72);
    lv_obj_set_user_data(s_btn_library_load, (void *)(uintptr_t)CTRL_DECK_1);
    lv_obj_add_event_cb(s_btn_library_load, library_load_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_remove_flag(s_btn_library_load, LV_OBJ_FLAG_CLICK_FOCUSABLE);

    lv_obj_t *lbl_load = lv_label_create(s_btn_library_load);
    lv_label_set_text(lbl_load, "LOAD D1");
    lv_obj_set_style_text_font(lbl_load, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_load, COL_ON_ACCENT, LV_PART_MAIN);
    lv_obj_align(lbl_load, LV_ALIGN_CENTER, 0, 0);

    s_btn_library_load_deck2 = lv_button_create(s_library_screen);
    lv_obj_remove_style_all(s_btn_library_load_deck2);
    lv_obj_add_style(s_btn_library_load_deck2, &s_style_btn_primary, LV_PART_MAIN);
    lv_obj_add_style(s_btn_library_load_deck2, &s_style_pressed, LV_STATE_PRESSED);
    lv_obj_set_size(s_btn_library_load_deck2, 72, 50);
    lv_obj_set_pos(s_btn_library_load_deck2, 708, 72);
    lv_obj_set_user_data(s_btn_library_load_deck2, (void *)(uintptr_t)CTRL_DECK_2);
    lv_obj_add_event_cb(s_btn_library_load_deck2, library_load_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_remove_flag(s_btn_library_load_deck2, LV_OBJ_FLAG_CLICK_FOCUSABLE);

    lv_obj_t *lbl_load_deck2 = lv_label_create(s_btn_library_load_deck2);
    lv_label_set_text(lbl_load_deck2, "LOAD D2");
    lv_obj_set_style_text_font(lbl_load_deck2, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_load_deck2, COL_ON_ACCENT, LV_PART_MAIN);
    lv_obj_align(lbl_load_deck2, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *btn_sort_artist = lv_button_create(s_library_screen);
    lv_obj_remove_style_all(btn_sort_artist);
    lv_obj_add_style(btn_sort_artist, &s_style_btn_secondary, LV_PART_MAIN);
    lv_obj_set_size(btn_sort_artist, 150, 45);
    lv_obj_set_pos(btn_sort_artist, 630, 132);
    lv_obj_add_event_cb(btn_sort_artist, library_sort_artist_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_remove_flag(btn_sort_artist, LV_OBJ_FLAG_CLICK_FOCUSABLE);

    lv_obj_t *lbl_sort_artist = lv_label_create(btn_sort_artist);
    lv_label_set_text(lbl_sort_artist, "SORT ARTIST");
    lv_obj_set_style_text_font(lbl_sort_artist, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_sort_artist, COL_TEXT_MUTED, LV_PART_MAIN);
    lv_obj_align(lbl_sort_artist, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *btn_sort_name = lv_button_create(s_library_screen);
    lv_obj_remove_style_all(btn_sort_name);
    lv_obj_add_style(btn_sort_name, &s_style_btn_secondary, LV_PART_MAIN);
    lv_obj_set_size(btn_sort_name, 150, 45);
    lv_obj_set_pos(btn_sort_name, 630, 187);
    lv_obj_add_event_cb(btn_sort_name, library_sort_name_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_remove_flag(btn_sort_name, LV_OBJ_FLAG_CLICK_FOCUSABLE);

    lv_obj_t *lbl_sort_name = lv_label_create(btn_sort_name);
    lv_label_set_text(lbl_sort_name, "SORT NAME");
    lv_obj_set_style_text_font(lbl_sort_name, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_sort_name, COL_TEXT_MUTED, LV_PART_MAIN);
    lv_obj_align(lbl_sort_name, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *btn_sort_bpm = lv_button_create(s_library_screen);
    lv_obj_remove_style_all(btn_sort_bpm);
    lv_obj_add_style(btn_sort_bpm, &s_style_btn_secondary, LV_PART_MAIN);
    lv_obj_set_size(btn_sort_bpm, 150, 45);
    lv_obj_set_pos(btn_sort_bpm, 630, 242);
    lv_obj_add_event_cb(btn_sort_bpm, library_sort_bpm_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_remove_flag(btn_sort_bpm, LV_OBJ_FLAG_CLICK_FOCUSABLE);

    lv_obj_t *lbl_sort_bpm = lv_label_create(btn_sort_bpm);
    lv_label_set_text(lbl_sort_bpm, "SORT BPM");
    lv_obj_set_style_text_font(lbl_sort_bpm, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_sort_bpm, COL_TEXT_MUTED, LV_PART_MAIN);
    lv_obj_align(lbl_sort_bpm, LV_ALIGN_CENTER, 0, 0);

    s_label_library_hint = lv_label_create(s_library_screen);
    lv_label_set_text(s_label_library_hint, "SELECT TRACK\nLOAD D1/D2");
    lv_obj_set_style_text_font(s_label_library_hint, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_label_library_hint, COL_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_pos(s_label_library_hint, 630, 300);
    ui_library_set_load_busy(false, NULL);

    return s_library_screen;
}

void ui_library_load_initial_track(void)
{
    mock_library_load_track_to_deck(0);
#ifdef WIN32
    library_track_t *track = library_get_ptr(0);
    if (track) {
        library_load_anlz(track);
        library_load_current_anlz(track);
        const anlz_metadata_t *meta = library_get_current_anlz();
        ui_library_apply_loaded_track(CTRL_DECK_1,
                                      track->title,
                                      track->artist,
                                      track->bpm,
                                      track->duration_ms,
                                      track->waveform_low,
                                      track->has_waveform != 0,
                                      meta);
    }
#else
    media_catalog_row_t row;
    media_loaded_track_t loaded;
    if (media_catalog_get_row(0, &row) == ESP_OK &&
        media_catalog_load(0, &loaded) == ESP_OK) {
        s_loaded_media[CTRL_DECK_1] = loaded;
        s_loaded_media_valid[CTRL_DECK_1] = true;
        s_loaded_media_source[CTRL_DECK_1] = loaded.source;
        const uint16_t bpm = loaded.bpm ? loaded.bpm : row.bpm;
        const anlz_metadata_t *meta = media_catalog_get_loaded_anlz_for_source(loaded.source);
        ui_library_apply_loaded_track(CTRL_DECK_1,
                                      row.title,
                                      row.artist,
                                      bpm,
                                      loaded.duration_ms,
                                      loaded.waveform_low,
                                      loaded.has_waveform != 0,
                                      meta);
    }
#endif
}

void ui_trigger_library_refresh(void)
{
    s_library_needs_refresh = true;
}

void ui_notify_usb_removed(void)
{
#ifndef WIN32
    s_usb_removed_pending = true;
#endif
}

void ui_refresh_library(void)
{
    if (!s_library_table) {
        return;
    }
    int n = ui_library_media_count();

#ifndef WIN32
    if (ui_diagnostics_enabled()) {
        ESP_LOGI(TAG, "ui_refresh_library start. Free SRAM: %d B, SPIRAM: %d B",
                 (int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    }
#endif

    ui_lvgl_lock();
    ui_library_cache_invalidate();
    lv_table_set_row_count(s_library_table, n + 1);

    for (int i = 0; i < n; i++) {
        ui_library_fill_row(i);
    }

    s_selected_track_idx = 0;
#ifdef WIN32
    const library_track_t *track = library_get_ptr(0);
    if (track) {
        ui_library_set_header_track(track->title, track->artist, track->bpm);
    }
#else
    media_catalog_row_t row;
    if (media_catalog_get_row(0, &row) == ESP_OK) {
        ui_library_set_header_track(row.title[0] ? row.title : "Unknown Title",
                                    row.artist[0] ? row.artist : "Unknown Artist",
                                    row.bpm);
    }
#endif
    ui_library_update_source_label();
    ui_lvgl_unlock();

#ifndef WIN32
    if (ui_diagnostics_enabled()) {
        ESP_LOGI(TAG, "ui_refresh_library end. Free SRAM: %d B, SPIRAM: %d B",
                 (int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    }
#endif

    ESP_LOGI(TAG, "library table refreshed: %d tracks", n);
}

bool ui_is_library_active(void)
{
    return s_active_tab == 1 && s_library_table != NULL;
}

esp_err_t ui_library_select_delta(int delta)
{
    if (delta == 0) {
        return ESP_OK;
    }
    if (!s_library_table) {
        return ESP_ERR_INVALID_STATE;
    }

    ui_lvgl_lock();
    int n = ui_library_media_count();
    if (n <= 0) {
        ui_lvgl_unlock();
        return ESP_ERR_NOT_FOUND;
    }

    int new_idx = s_selected_track_idx + delta;
    if (new_idx < 0) new_idx = 0;
    if (new_idx >= n) new_idx = n - 1;
    if (new_idx == s_selected_track_idx) {
        lv_table_set_selected_cell(s_library_table, s_selected_track_idx + 1, 0);
        ui_lvgl_unlock();
        return ESP_OK;
    }

    int old_idx = s_selected_track_idx;
    s_selected_track_idx = new_idx;
    ui_library_fill_row(old_idx);
    ui_library_fill_row(new_idx);
    lv_table_set_selected_cell(s_library_table, s_selected_track_idx + 1, 0);
    ui_lvgl_unlock();
    return ESP_OK;
}

esp_err_t ui_library_load_selected(void)
{
    return ui_library_load_selected_for_deck(CTRL_DECK_1);
}

esp_err_t ui_library_load_selected_for_deck(uint8_t deck)
{
    if (!s_library_table) {
        return ESP_ERR_INVALID_STATE;
    }
    if (ui_library_media_count() <= 0) {
        return ESP_ERR_NOT_FOUND;
    }
    if (s_track_load_busy) {
        return ESP_ERR_INVALID_STATE;
    }

    ui_lvgl_lock();
    uint8_t old_deck = s_library_load_request_deck;
    s_library_load_request_deck = deck;
    library_load_event_cb(NULL);
    s_library_load_request_deck = old_deck;
    ui_lvgl_unlock();
    return ESP_OK;
}

void ui_library_update(const ui_frame_context_t *ctx)
{
    int active_tab = ctx ? ctx->active_tab : 0;
    s_active_tab = active_tab;
    ui_library_update_plan_t plan =
        ui_library_plan_update(active_tab, s_library_needs_refresh,
#ifndef WIN32
                               s_usb_removed_pending
#else
                               false
#endif
        );

#ifndef WIN32
    if (plan.apply_usb_removed) {
        ui_apply_usb_removed();
    }
    if (plan.poll_track_load_result) {
        ui_poll_track_load_result();
    }
#endif

    if (plan.refresh_library) {
        s_library_needs_refresh = false;
        ui_refresh_library();
    }

    if (plan.focus_library_table && s_library_table) {
        lv_group_t *g = lv_group_get_default();
        if (g && lv_group_get_focused(g) != s_library_table) {
            lv_group_focus_obj(s_library_table);
        }

        uint32_t sel_row = LV_TABLE_CELL_NONE;
        uint32_t sel_col = LV_TABLE_CELL_NONE;
        lv_table_get_selected_cell(s_library_table, &sel_row, &sel_col);
        if (sel_row == LV_TABLE_CELL_NONE || sel_row == 0) {
            lv_table_set_selected_cell(s_library_table, s_selected_track_idx + 1, 0);
        }
    }
}

uint32_t ui_library_deck_duration_ms(uint8_t deck, uint32_t fallback_duration_ms)
{
#ifndef WIN32
    uint8_t idx = ui_library_deck_index(deck);
    if (s_loaded_media_valid[idx]) return s_loaded_media[idx].duration_ms;
#else
    (void)deck;
#endif
    return fallback_duration_ms;
}

uint16_t ui_library_deck_bpm(uint8_t deck, uint16_t fallback_bpm)
{
#ifndef WIN32
    uint8_t idx = ui_library_deck_index(deck);
    if (s_loaded_media_valid[idx] && s_loaded_media[idx].bpm > 0) return s_loaded_media[idx].bpm;
#else
    (void)deck;
#endif
    return fallback_bpm;
}

bool ui_library_has_remote_loaded_track(void)
{
#ifndef WIN32
    for (uint8_t deck = 0; deck < DECK_CORE_DECK_COUNT; deck++) {
        if (s_loaded_media_valid[deck] && s_loaded_media_source[deck] == MEDIA_SOURCE_REMOTE_LINK) {
            return true;
        }
    }
#endif
    return false;
}

bool ui_library_get_loaded_waveform(uint8_t deck,
                                    const uint8_t **waveform_low,
                                    bool *has_waveform)
{
    if (waveform_low) {
        *waveform_low = NULL;
    }
    if (has_waveform) {
        *has_waveform = false;
    }
#ifndef WIN32
    uint8_t idx = ui_library_deck_index(deck);
    if (!s_loaded_media_valid[idx]) {
        return false;
    }
    if (waveform_low) {
        *waveform_low = s_loaded_media[idx].waveform_low;
    }
    if (has_waveform) {
        *has_waveform = s_loaded_media[idx].has_waveform != 0;
    }
    return true;
#else
    (void)deck;
    return false;
#endif
}

#endif
