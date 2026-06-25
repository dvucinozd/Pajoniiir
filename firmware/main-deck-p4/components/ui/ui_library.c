#include "ui_library.h"
#include "ui_diagnostics.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static void ui_library_copy_str(char *dest, size_t dest_size, const char *src)
{
    if (!dest || dest_size == 0) {
        return;
    }
    dest[0] = '\0';
    if (!src) {
        return;
    }
    size_t i = 0;
    while (i + 1u < dest_size && src[i] != '\0') {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

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
        ui_library_copy_str(dest, dest_size, src);
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
                                const char *key,
                                uint16_t bpm,
                                uint32_t duration_ms)
{
    if (!out) {
        return;
    }

    ui_library_truncate_str(out->title, sizeof(out->title), title, 26);
    ui_library_truncate_str(out->artist, sizeof(out->artist), artist, 18);
    strncpy(out->key, key ? key : "", sizeof(out->key) - 1);
    out->key[sizeof(out->key) - 1] = '\0';

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
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "media_catalog.h"

#define UI_TRACK_LOAD_STACK (16 * 1024)
#endif

static const char *TAG = "ui_library";

static ui_library_config_t s_library_config;
static lv_obj_t *s_library_screen = NULL;
static lv_obj_t *s_library_table = NULL;
static lv_obj_t *s_label_library_source = NULL;
static lv_obj_t *s_btn_library_load = NULL;
static lv_obj_t *s_btn_library_load_deck2 = NULL;
static lv_obj_t *s_active_deck_indicator = NULL;
static lv_obj_t *s_label_indicator_deck = NULL;
static lv_obj_t *s_label_indicator_status = NULL;
static int s_active_tab = 0;
static int s_selected_track_idx = 0;
static volatile bool s_library_needs_refresh = false;
static bool s_sort_artist_desc = false;
static bool s_sort_name_desc = false;
static bool s_sort_bpm_desc = false;
static bool s_sort_key_desc = false;
static bool s_track_load_busy = false;
static uint8_t s_library_load_request_deck = CTRL_DECK_1;

static uint32_t s_deck_loaded_track_key[DECK_CORE_DECK_COUNT] = {0, 0};
static bool s_deck_loaded_track_valid[DECK_CORE_DECK_COUNT] = {false, false};

#ifndef WIN32
static portMUX_TYPE s_track_load_lock = portMUX_INITIALIZER_UNLOCKED;
static media_loaded_track_t s_loaded_media[DECK_CORE_DECK_COUNT];
static bool s_loaded_media_valid[DECK_CORE_DECK_COUNT];
static QueueHandle_t s_track_load_result_q = NULL;
static volatile bool s_usb_removed_pending = false;

typedef struct {
    int index;
    uint8_t deck;
    uint32_t generation;
    media_catalog_track_t item;
    media_loaded_track_t loaded;
    esp_err_t rc;
    char status[40];
} ui_track_load_result_t;

typedef struct {
    int index;
    uint8_t deck;
    uint32_t generation;
} ui_track_load_request_t;

#endif

#ifndef WIN32
static void ui_track_load_set_status(ui_track_load_result_t *result,
                                     const char *status,
                                     const char *fallback)
{
    const char *text = (status && status[0]) ? status : fallback;
    snprintf(result->status, sizeof(result->status), "%.*s",
             (int)sizeof(result->status) - 1,
             text ? text : "");
}
#endif

static bool ui_library_try_begin_track_load(void)
{
#ifndef WIN32
    bool accepted = false;
    portENTER_CRITICAL(&s_track_load_lock);
    if (!s_track_load_busy) {
        s_track_load_busy = true;
        accepted = true;
    }
    portEXIT_CRITICAL(&s_track_load_lock);
    return accepted;
#else
    if (s_track_load_busy) {
        return false;
    }
    s_track_load_busy = true;
    return true;
#endif
}

static void ui_library_finish_track_load(void)
{
#ifndef WIN32
    portENTER_CRITICAL(&s_track_load_lock);
    s_track_load_busy = false;
    portEXIT_CRITICAL(&s_track_load_lock);
#else
    s_track_load_busy = false;
#endif
}

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

    (void)hint;
}

static void ui_library_update_source_label(void)
{
    if (!s_label_library_source) {
        return;
    }
#ifndef WIN32
    lv_label_set_text_fmt(s_label_library_source, "LOCAL USB  %d TRACKS", media_catalog_count());
#else
    lv_label_set_text_fmt(s_label_library_source, "LOCAL USB  %d TRACKS", library_count());
#endif
}

static void ui_library_fill_row(int i)
{
    const char *title = NULL;
    const char *artist = NULL;
    const char *key = NULL;
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
    key = row.key;
#else
    const library_track_t *track = library_get_ptr(i);
    if (!track) {
        return;
    }
    title = track->title;
    artist = track->artist;
    bpm = track->bpm;
    duration_ms = track->duration_ms;
    key = track->key;
#endif

    ui_library_row_text_t text;
    ui_library_format_row_text(&text, title, artist, key, bpm, duration_ms);
    lv_table_set_cell_value(s_library_table, i, 0, text.title);
    lv_table_set_cell_value(s_library_table, i, 1, text.artist);
    lv_table_set_cell_value(s_library_table, i, 2, text.key);
    lv_table_set_cell_value(s_library_table, i, 3, text.bpm);
    lv_table_set_cell_value(s_library_table, i, 4, text.duration);
}

static void ui_library_populate_rows(void)
{
    if (!s_library_table) {
        return;
    }
    int n_tracks = ui_library_media_count();
    lv_table_set_row_count(s_library_table, n_tracks);
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

    ui_track_load_result_t *result = heap_caps_calloc(1, sizeof(*result),
                                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!result) {
        result = calloc(1, sizeof(*result));
    }
    if (!result) {
        ESP_LOGE(TAG, "track load result allocation failed");
        ui_library_finish_track_load();
        vTaskDelete(NULL);
        return;
    }

    result->index = req.index;
    result->deck = req.deck;
    result->generation = req.generation;
    result->rc = ESP_OK;

    if (media_catalog_get(req.index, &result->item) != ESP_OK) {
        result->rc = ESP_ERR_NOT_FOUND;
        ui_track_load_set_status(result, "NO TRACK", "NO TRACK");
    } else {
        result->rc = media_catalog_load(req.index, &result->loaded);
        if (result->rc != ESP_OK) {
            ui_track_load_set_status(result, "LOAD ERR", "LOAD ERR");
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
                audio_engine_deck_status_t deck_status = {0};
                const char *audio_err = NULL;
                if (audio_engine_deck_get_status(req.deck, &deck_status) == ESP_OK) {
                    audio_err = deck_status.last_error_text;
                }
                ui_track_load_set_status(result, audio_err, "AUDIO ERR");
            } else {
                ui_track_load_set_status(result, "TRACK LOADED", "TRACK LOADED");
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
    free(result);
    vTaskDelete(NULL);
}

static esp_err_t ui_submit_track_load(int index, uint8_t deck)
{
    if (!s_track_load_result_q) {
        s_track_load_result_q = xQueueCreate(1, sizeof(ui_track_load_result_t));
    }
    if (!s_track_load_result_q) {
        ui_library_status_hold("NO QUEUE", COL_RED, 2500);
        ui_library_set_load_busy(false, "NO QUEUE");
        ui_library_finish_track_load();
        return ESP_ERR_NO_MEM;
    }

    xQueueReset(s_track_load_result_q);

    ui_track_load_request_t *req = malloc(sizeof(*req));
    if (!req) {
        ui_library_status_hold("NO MEM", COL_RED, 2500);
        ui_library_set_load_busy(false, "NO MEM");
        ui_library_finish_track_load();
        return ESP_ERR_NO_MEM;
    }
    req->index = index;
    req->deck = deck;
    req->generation = library_generation();

    if (xTaskCreate(ui_track_load_worker, "ui_load", UI_TRACK_LOAD_STACK, req, 3, NULL) != pdPASS) {
        free(req);
        ui_library_status_hold("NO TASK", COL_RED, 2500);
        ui_library_set_load_busy(false, "NO TASK");
        ui_library_finish_track_load();
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static void ui_apply_usb_removed(void)
{
    s_usb_removed_pending = false;
    bool removed_loaded = false;
    for (uint8_t deck = 0; deck < DECK_CORE_DECK_COUNT; deck++) {
        if (s_loaded_media_valid[deck]) {
            s_loaded_media_valid[deck] = false;
            s_deck_loaded_track_valid[deck] = false;
            s_deck_loaded_track_key[deck] = 0;
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
        if (s_library_table) {
            lv_obj_invalidate(s_library_table);
        }
        ui_library_status_hold("USB REMOVED", COL_AMBER, 2500);
    }
    ui_library_set_load_busy(false, "USB REMOVED");
    ui_library_finish_track_load();
}

static void ui_poll_track_load_result(void)
{
    if (!s_track_load_result_q) return;

    ui_track_load_result_t result;
    while (xQueueReceive(s_track_load_result_q, &result, 0) == pdTRUE) {
        bool stale = result.generation != library_generation();
        if (stale) {
            ui_library_set_load_busy(false, "USB REMOVED");
            ui_library_finish_track_load();
            continue;
        }

        if (result.rc != ESP_OK) {
            const char *display = result.status[0] ? result.status : "LOAD ERR";
            ESP_LOGW(TAG, "track load worker failed index=%d: %s", result.index, esp_err_to_name(result.rc));
            ui_library_status_hold(display, ui_library_status_color_for_text(display), 3500);
            ui_library_set_load_busy(false, display);
            ui_library_finish_track_load();
            continue;
        }

        mock_library_load_track_to_deck(result.index);
        uint8_t deck = ui_library_deck_index(result.deck);
        s_loaded_media[deck] = result.loaded;
        s_loaded_media_valid[deck] = true;
        s_deck_loaded_track_key[deck] = result.loaded.track_key;
        s_deck_loaded_track_valid[deck] = true;
        ESP_LOGI("UI_HIGHLIGHT", "POLL RESULT: deck=%u, key=0x%08X", (unsigned)deck, (unsigned)result.loaded.track_key);
        if (s_library_table) {
            lv_obj_invalidate(s_library_table);
        }
        const uint16_t bpm = result.loaded.bpm ? result.loaded.bpm : result.item.bpm;
        const anlz_metadata_t *meta = media_catalog_get_loaded_anlz();
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
        ui_library_finish_track_load();
    }
}

#endif

static void ui_library_load_selected_deck(uint8_t deck)
{
    if (!ui_library_try_begin_track_load()) {
        ui_library_status_hold("LOAD BUSY", COL_AMBER, 1200);
        return;
    }
    ui_library_set_load_busy(true, "LOAD BUSY");

#ifdef WIN32
    library_track_t *track = library_get_ptr(s_selected_track_idx);
    if (!track) {
        ui_library_set_load_busy(false, NULL);
        ui_library_finish_track_load();
        return;
    }

    mock_library_load_track_to_deck(s_selected_track_idx);
    library_load_anlz(track);
    library_load_current_anlz(track);
    const anlz_metadata_t *meta = library_get_current_anlz();
    s_deck_loaded_track_key[deck] = track->track_id;
    s_deck_loaded_track_valid[deck] = true;
    if (s_library_table) {
        lv_obj_invalidate(s_library_table);
    }
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
        ui_library_finish_track_load();
        return;
    }

    ui_library_status_hold("LOADING", COL_ACCENT, 1500);
    (void)ui_submit_track_load(s_selected_track_idx, deck);
    return;
#endif
    ui_library_status_hold("TRACK LOADED", COL_GREEN, 2000);
    ui_library_set_load_busy(false, "TRACK LOADED");
    ui_library_finish_track_load();
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
    lv_table_set_selected_cell(s_library_table, s_selected_track_idx, 0);
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
    lv_table_set_selected_cell(s_library_table, s_selected_track_idx, 0);
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
    lv_table_set_selected_cell(s_library_table, s_selected_track_idx, 0);
}

static void library_sort_key_event_cb(lv_event_t *e)
{
    (void)e;
    if (!s_library_table) return;
    uint32_t target_key = ui_library_selected_key();
    s_sort_key_desc = !s_sort_key_desc;
#ifndef WIN32
    media_catalog_sort(3, s_sort_key_desc);
#else
    library_sort(3, s_sort_key_desc);
#endif
    ui_refresh_library();
    ui_library_preserve_selection_by_key(target_key);
    lv_table_set_selected_cell(s_library_table, s_selected_track_idx, 0);
}

static void library_table_event_cb(lv_event_t *e)
{
    lv_obj_t *table = lv_event_get_target(e);
    uint32_t row;
    uint32_t col;
    lv_table_get_selected_cell(table, &row, &col);

    if ((int)row >= 0 && (int)row < ui_library_media_count()) {
        int new_idx = (int)row;
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

static void library_table_draw_part_begin_cb(lv_event_t *e)
{
    lv_draw_task_t * draw_task = lv_event_get_draw_task(e);
    if (!draw_task) return;

    lv_draw_dsc_base_t * base_dsc = (lv_draw_dsc_base_t *)lv_draw_task_get_draw_dsc(draw_task);
    if (!base_dsc) return;

    if (base_dsc->part == LV_PART_ITEMS) {
        uint32_t row = base_dsc->id1;

        uint32_t track_key = 0;
        bool has_track = false;

#ifndef WIN32
        media_catalog_row_t row_data;
        if (media_catalog_get_row((int)row, &row_data) == ESP_OK) {
            track_key = row_data.track_key;
            has_track = true;
        }
#else
        const library_track_t *track = library_get_ptr((int)row);
        if (track) {
            track_key = track->track_id;
            has_track = true;
        }
#endif

        if (has_track && track_key != 0) {
            bool loaded_d1 = s_deck_loaded_track_valid[CTRL_DECK_1] && (s_deck_loaded_track_key[CTRL_DECK_1] == track_key);
            bool loaded_d2 = s_deck_loaded_track_valid[CTRL_DECK_2] && (s_deck_loaded_track_key[CTRL_DECK_2] == track_key);

            lv_obj_t *obj = lv_event_get_target(e);
            uint32_t sel_row = LV_TABLE_CELL_NONE;
            uint32_t sel_col = LV_TABLE_CELL_NONE;
            lv_table_get_selected_cell(obj, &sel_row, &sel_col);

            lv_draw_task_type_t task_type = lv_draw_task_get_type(draw_task);

            lv_color_t bg_color;
            bool is_loaded = false;
            if (loaded_d1 || loaded_d2) {
                is_loaded = true;
                if (loaded_d1 && loaded_d2) {
                    bg_color = lv_color_hex(0x4DB37A); // Mješavina plave i zelene
                } else if (loaded_d1) {
                    bg_color = COL_ACCENT; // Plava
                } else {
                    bg_color = COL_GREEN; // Zelena
                }
            }

            if (is_loaded) {
                // Učitana pjesma (bilo selektirana ili ne): puni highlight u boji decka i crna slova
                if (task_type == LV_DRAW_TASK_TYPE_FILL) {
                    lv_draw_fill_dsc_t * fill_dsc = (lv_draw_fill_dsc_t *)base_dsc;
                    fill_dsc->color = bg_color;
                    fill_dsc->opa = LV_OPA_80;
                }
                else if (task_type == LV_DRAW_TASK_TYPE_BORDER) {
                    lv_draw_border_dsc_t * border_dsc = (lv_draw_border_dsc_t *)base_dsc;
                    border_dsc->color = bg_color;
                }
                else if (task_type == LV_DRAW_TASK_TYPE_LABEL) {
                    lv_draw_label_dsc_t * label_dsc = (lv_draw_label_dsc_t *)base_dsc;
                    label_dsc->color = COL_ON_ACCENT;
                }
            } else if (sel_row == row) {
                // Neučitana selektirana pjesma: bijeli highlight i crna slova
                if (task_type == LV_DRAW_TASK_TYPE_FILL) {
                    lv_draw_fill_dsc_t * fill_dsc = (lv_draw_fill_dsc_t *)base_dsc;
                    fill_dsc->color = COL_TABLE_ALT;
                    fill_dsc->opa = LV_OPA_80;
                }
                else if (task_type == LV_DRAW_TASK_TYPE_BORDER) {
                    lv_draw_border_dsc_t * border_dsc = (lv_draw_border_dsc_t *)base_dsc;
                    border_dsc->color = COL_ACCENT;
                }
                else if (task_type == LV_DRAW_TASK_TYPE_LABEL) {
                    lv_draw_label_dsc_t * label_dsc = (lv_draw_label_dsc_t *)base_dsc;
                    label_dsc->color = COL_ON_ACCENT;
                }
            }
        }
    }
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

    // Fixed header table to prevent it from scrolling away
    lv_obj_t *s_library_header_table = lv_table_create(s_library_screen);
    lv_obj_remove_style_all(s_library_header_table);
    lv_obj_set_size(s_library_header_table, 630, 36);
    lv_obj_set_pos(s_library_header_table, 10, 10);
    lv_obj_clear_flag(s_library_header_table, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_library_header_table, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_style_min_height(s_library_header_table, 36, LV_PART_ITEMS);
    lv_obj_set_style_max_height(s_library_header_table, 36, LV_PART_ITEMS);
    lv_obj_set_style_pad_left(s_library_header_table, 6, LV_PART_ITEMS);
    lv_obj_set_style_pad_right(s_library_header_table, 6, LV_PART_ITEMS);
    lv_obj_set_style_pad_top(s_library_header_table, 8, LV_PART_ITEMS);
    lv_obj_set_style_pad_bottom(s_library_header_table, 8, LV_PART_ITEMS);
    lv_obj_set_style_border_color(s_library_header_table, COL_BORDER, LV_PART_ITEMS);
    lv_obj_set_style_border_width(s_library_header_table, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_side(s_library_header_table, LV_BORDER_SIDE_BOTTOM, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(s_library_header_table, COL_PANEL_DK, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(s_library_header_table, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_text_color(s_library_header_table, COL_ACCENT, LV_PART_ITEMS);
    lv_obj_set_style_text_font(s_library_header_table, &lv_font_montserrat_14, LV_PART_ITEMS);

    lv_table_set_column_width(s_library_header_table, 0, 280);
    lv_table_set_column_width(s_library_header_table, 1, 160);
    lv_table_set_column_width(s_library_header_table, 2, 70);
    lv_table_set_column_width(s_library_header_table, 3, 55);
    lv_table_set_column_width(s_library_header_table, 4, 65);
    lv_table_set_column_count(s_library_header_table, 5);
    lv_table_set_row_count(s_library_header_table, 1);
    lv_table_set_cell_value(s_library_header_table, 0, 0, "TITLE");
    lv_table_set_cell_value(s_library_header_table, 0, 1, "ARTIST");
    lv_table_set_cell_value(s_library_header_table, 0, 2, "KEY");
    lv_table_set_cell_value(s_library_header_table, 0, 3, "BPM");
    lv_table_set_cell_value(s_library_header_table, 0, 4, "TIME");

    // Main scrollable tracks table (height stretched to the bottom of the screen)
    s_library_table = lv_table_create(s_library_screen);
    lv_obj_set_size(s_library_table, 630, 378);
    lv_obj_set_pos(s_library_table, 10, 46);
    lv_obj_add_event_cb(s_library_table, library_table_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_library_table, library_table_draw_part_begin_cb, LV_EVENT_DRAW_TASK_ADDED, NULL);
    lv_obj_add_flag(s_library_table, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);

    lv_obj_set_style_min_height(s_library_table, 40, LV_PART_ITEMS);
    lv_obj_set_style_max_height(s_library_table, 40, LV_PART_ITEMS);
    lv_obj_set_style_pad_left(s_library_table, 6, LV_PART_ITEMS);
    lv_obj_set_style_pad_right(s_library_table, 6, LV_PART_ITEMS);
    lv_obj_set_style_pad_top(s_library_table, 8, LV_PART_ITEMS);
    lv_obj_set_style_pad_bottom(s_library_table, 8, LV_PART_ITEMS);
    lv_obj_set_style_border_color(s_library_table, COL_BORDER, LV_PART_ITEMS);
    lv_obj_set_style_border_width(s_library_table, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_side(s_library_table, LV_BORDER_SIDE_BOTTOM, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(s_library_table, COL_TABLE_ROW, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(s_library_table, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_text_color(s_library_table, COL_TEXT_MUTED, LV_PART_ITEMS);
    lv_obj_set_style_text_font(s_library_table, &lv_font_montserrat_18, LV_PART_ITEMS);
    lv_obj_set_style_border_color(s_library_table, COL_ACCENT, LV_PART_ITEMS | LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(s_library_table, 3, LV_PART_ITEMS | LV_STATE_FOCUSED);
    lv_obj_set_style_border_side(s_library_table, LV_BORDER_SIDE_BOTTOM, LV_PART_ITEMS | LV_STATE_FOCUSED);
    lv_obj_set_style_border_opa(s_library_table, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_FOCUSED);
    lv_obj_set_style_text_color(s_library_table, COL_ON_ACCENT, LV_PART_ITEMS | LV_STATE_FOCUSED);
    lv_table_set_column_width(s_library_table, 0, 280);
    lv_table_set_column_width(s_library_table, 1, 160);
    lv_table_set_column_width(s_library_table, 2, 70);
    lv_table_set_column_width(s_library_table, 3, 55);
    lv_table_set_column_width(s_library_table, 4, 65);
    lv_table_set_column_count(s_library_table, 5);
    ui_library_populate_rows();

    // Active Deck Indicator (130x90, reduced height and width)
    s_active_deck_indicator = lv_obj_create(s_library_screen);
    lv_obj_remove_style_all(s_active_deck_indicator);
    lv_obj_set_size(s_active_deck_indicator, 130, 90);
    lv_obj_set_pos(s_active_deck_indicator, 660, 10);
    lv_obj_set_style_bg_opa(s_active_deck_indicator, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_active_deck_indicator, 10, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_active_deck_indicator, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_active_deck_indicator, COL_BORDER, LV_PART_MAIN);

    s_label_indicator_deck = lv_label_create(s_active_deck_indicator);
    lv_label_set_text(s_label_indicator_deck, "DECK 1");
    lv_obj_set_style_text_font(s_label_indicator_deck, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_label_indicator_deck, COL_ON_ACCENT, LV_PART_MAIN);
    lv_obj_align(s_label_indicator_deck, LV_ALIGN_CENTER, 0, -12);

    s_label_indicator_status = lv_label_create(s_active_deck_indicator);
    lv_label_set_text(s_label_indicator_status, "ACTIVE");
    lv_obj_set_style_text_font(s_label_indicator_status, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_label_indicator_status, COL_ON_ACCENT, LV_PART_MAIN);
    lv_obj_align(s_label_indicator_status, LV_ALIGN_CENTER, 0, 18);

    // Stacks LOAD D1/D2 vertically with specific accent coloring, pushed down
    s_btn_library_load = lv_button_create(s_library_screen);
    lv_obj_remove_style_all(s_btn_library_load);
    lv_obj_add_style(s_btn_library_load, &s_style_btn_primary, LV_PART_MAIN);
    lv_obj_add_style(s_btn_library_load, &s_style_pressed, LV_STATE_PRESSED);
    lv_obj_set_size(s_btn_library_load, 130, 45);
    lv_obj_set_pos(s_btn_library_load, 660, 110);
    lv_obj_set_user_data(s_btn_library_load, (void *)(uintptr_t)CTRL_DECK_1);
    lv_obj_add_event_cb(s_btn_library_load, library_load_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_remove_flag(s_btn_library_load, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_set_style_bg_color(s_btn_library_load, COL_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_btn_library_load, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *lbl_load = lv_label_create(s_btn_library_load);
    lv_label_set_text(lbl_load, "LOAD DECK 1");
    lv_obj_set_style_text_font(lbl_load, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_load, COL_ON_ACCENT, LV_PART_MAIN);
    lv_obj_align(lbl_load, LV_ALIGN_CENTER, 0, 0);

    s_btn_library_load_deck2 = lv_button_create(s_library_screen);
    lv_obj_remove_style_all(s_btn_library_load_deck2);
    lv_obj_add_style(s_btn_library_load_deck2, &s_style_btn_primary, LV_PART_MAIN);
    lv_obj_add_style(s_btn_library_load_deck2, &s_style_pressed, LV_STATE_PRESSED);
    lv_obj_set_size(s_btn_library_load_deck2, 130, 45);
    lv_obj_set_pos(s_btn_library_load_deck2, 660, 160);
    lv_obj_set_user_data(s_btn_library_load_deck2, (void *)(uintptr_t)CTRL_DECK_2);
    lv_obj_add_event_cb(s_btn_library_load_deck2, library_load_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_remove_flag(s_btn_library_load_deck2, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_set_style_bg_color(s_btn_library_load_deck2, COL_GREEN, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_btn_library_load_deck2, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *lbl_load_deck2 = lv_label_create(s_btn_library_load_deck2);
    lv_label_set_text(lbl_load_deck2, "LOAD DECK 2");
    lv_obj_set_style_text_font(lbl_load_deck2, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_load_deck2, COL_ON_ACCENT, LV_PART_MAIN);
    lv_obj_align(lbl_load_deck2, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *btn_sort_artist = lv_button_create(s_library_screen);
    lv_obj_remove_style_all(btn_sort_artist);
    lv_obj_add_style(btn_sort_artist, &s_style_btn_secondary, LV_PART_MAIN);
    lv_obj_set_size(btn_sort_artist, 130, 40);
    lv_obj_set_pos(btn_sort_artist, 660, 215);
    lv_obj_add_event_cb(btn_sort_artist, library_sort_artist_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_remove_flag(btn_sort_artist, LV_OBJ_FLAG_CLICK_FOCUSABLE);

    lv_obj_t *lbl_sort_artist = lv_label_create(btn_sort_artist);
    lv_label_set_text(lbl_sort_artist, "SORT ARTIST");
    lv_obj_set_style_text_font(lbl_sort_artist, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_sort_artist, COL_TEXT_MUTED, LV_PART_MAIN);
    lv_obj_align(lbl_sort_artist, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *btn_sort_name = lv_button_create(s_library_screen);
    lv_obj_remove_style_all(btn_sort_name);
    lv_obj_add_style(btn_sort_name, &s_style_btn_secondary, LV_PART_MAIN);
    lv_obj_set_size(btn_sort_name, 130, 40);
    lv_obj_set_pos(btn_sort_name, 660, 260);
    lv_obj_add_event_cb(btn_sort_name, library_sort_name_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_remove_flag(btn_sort_name, LV_OBJ_FLAG_CLICK_FOCUSABLE);

    lv_obj_t *lbl_sort_name = lv_label_create(btn_sort_name);
    lv_label_set_text(lbl_sort_name, "SORT NAME");
    lv_obj_set_style_text_font(lbl_sort_name, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_sort_name, COL_TEXT_MUTED, LV_PART_MAIN);
    lv_obj_align(lbl_sort_name, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *btn_sort_bpm = lv_button_create(s_library_screen);
    lv_obj_remove_style_all(btn_sort_bpm);
    lv_obj_add_style(btn_sort_bpm, &s_style_btn_secondary, LV_PART_MAIN);
    lv_obj_set_size(btn_sort_bpm, 130, 40);
    lv_obj_set_pos(btn_sort_bpm, 660, 305);
    lv_obj_add_event_cb(btn_sort_bpm, library_sort_bpm_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_remove_flag(btn_sort_bpm, LV_OBJ_FLAG_CLICK_FOCUSABLE);

    lv_obj_t *lbl_sort_bpm = lv_label_create(btn_sort_bpm);
    lv_label_set_text(lbl_sort_bpm, "SORT BPM");
    lv_obj_set_style_text_font(lbl_sort_bpm, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_sort_bpm, COL_TEXT_MUTED, LV_PART_MAIN);
    lv_obj_align(lbl_sort_bpm, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *btn_sort_key = lv_button_create(s_library_screen);
    lv_obj_remove_style_all(btn_sort_key);
    lv_obj_add_style(btn_sort_key, &s_style_btn_secondary, LV_PART_MAIN);
    lv_obj_set_size(btn_sort_key, 130, 40);
    lv_obj_set_pos(btn_sort_key, 660, 350);
    lv_obj_add_event_cb(btn_sort_key, library_sort_key_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_remove_flag(btn_sort_key, LV_OBJ_FLAG_CLICK_FOCUSABLE);

    lv_obj_t *lbl_sort_key = lv_label_create(btn_sort_key);
    lv_label_set_text(lbl_sort_key, "SORT KEY");
    lv_obj_set_style_text_font(lbl_sort_key, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_sort_key, COL_TEXT_MUTED, LV_PART_MAIN);
    lv_obj_align(lbl_sort_key, LV_ALIGN_CENTER, 0, 0);

    // s_label_library_hint has been removed
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
        s_deck_loaded_track_key[CTRL_DECK_1] = track->track_id;
        s_deck_loaded_track_valid[CTRL_DECK_1] = true;
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
        s_deck_loaded_track_key[CTRL_DECK_1] = loaded.track_key;
        s_deck_loaded_track_valid[CTRL_DECK_1] = true;
        const uint16_t bpm = loaded.bpm ? loaded.bpm : row.bpm;
        const anlz_metadata_t *meta = media_catalog_get_loaded_anlz();
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
    if (s_library_table) {
        lv_obj_invalidate(s_library_table);
    }
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
    lv_table_set_row_count(s_library_table, n);

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
        lv_table_set_selected_cell(s_library_table, s_selected_track_idx, 0);
        ui_lvgl_unlock();
        return ESP_OK;
    }

    int old_idx = s_selected_track_idx;
    s_selected_track_idx = new_idx;
    ui_library_fill_row(old_idx);
    ui_library_fill_row(new_idx);
    lv_table_set_selected_cell(s_library_table, s_selected_track_idx, 0);
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

uint32_t ui_library_loaded_track_key_for_deck(uint8_t deck)
{
    if (deck >= DECK_CORE_DECK_COUNT || !s_deck_loaded_track_valid[deck]) {
        return 0;
    }
    return s_deck_loaded_track_key[deck];
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
        if (sel_row == LV_TABLE_CELL_NONE) {
            lv_table_set_selected_cell(s_library_table, s_selected_track_idx, 0);
        }
    }

    if (ctx && s_active_deck_indicator && s_label_indicator_deck && s_label_indicator_status) {
        bool d1_playing = ctx->deck_state[CTRL_DECK_1].playing;
        bool d2_playing = ctx->deck_state[CTRL_DECK_2].playing;

        if (d1_playing && d2_playing) {
            lv_obj_set_style_bg_color(s_active_deck_indicator, COL_RED, LV_PART_MAIN);
            lv_obj_set_style_border_color(s_active_deck_indicator, COL_BORDER, LV_PART_MAIN);
            lv_obj_set_style_border_width(s_active_deck_indicator, 1, LV_PART_MAIN);
            lv_label_set_text(s_label_indicator_deck, "DECK 1+2");
            lv_label_set_text(s_label_indicator_status, "ACTIVE");
            lv_obj_set_style_text_color(s_label_indicator_deck, COL_ON_ACCENT, LV_PART_MAIN);
            lv_obj_set_style_text_color(s_label_indicator_status, COL_ON_ACCENT, LV_PART_MAIN);
        } else if (d1_playing) {
            lv_obj_set_style_bg_color(s_active_deck_indicator, COL_ACCENT, LV_PART_MAIN);
            lv_obj_set_style_border_color(s_active_deck_indicator, COL_BORDER, LV_PART_MAIN);
            lv_obj_set_style_border_width(s_active_deck_indicator, 1, LV_PART_MAIN);
            lv_label_set_text(s_label_indicator_deck, "DECK 1");
            lv_label_set_text(s_label_indicator_status, "ACTIVE");
            lv_obj_set_style_text_color(s_label_indicator_deck, COL_ON_ACCENT, LV_PART_MAIN);
            lv_obj_set_style_text_color(s_label_indicator_status, COL_ON_ACCENT, LV_PART_MAIN);
        } else if (d2_playing) {
            lv_obj_set_style_bg_color(s_active_deck_indicator, COL_GREEN, LV_PART_MAIN);
            lv_obj_set_style_border_color(s_active_deck_indicator, COL_BORDER, LV_PART_MAIN);
            lv_obj_set_style_border_width(s_active_deck_indicator, 1, LV_PART_MAIN);
            lv_label_set_text(s_label_indicator_deck, "DECK 2");
            lv_label_set_text(s_label_indicator_status, "ACTIVE");
            lv_obj_set_style_text_color(s_label_indicator_deck, COL_ON_ACCENT, LV_PART_MAIN);
            lv_obj_set_style_text_color(s_label_indicator_status, COL_ON_ACCENT, LV_PART_MAIN);
        } else {
            // Nijedan ne svira: prikazujemo target deck u neaktivnom/ready stanju
            uint8_t target = ctx->active_deck;
            lv_color_t target_color = (target == CTRL_DECK_1) ? COL_ACCENT : COL_GREEN;
            
            lv_obj_set_style_bg_color(s_active_deck_indicator, COL_PANEL_DK, LV_PART_MAIN);
            lv_obj_set_style_border_color(s_active_deck_indicator, target_color, LV_PART_MAIN);
            lv_obj_set_style_border_width(s_active_deck_indicator, 2, LV_PART_MAIN);
            
            if (target == CTRL_DECK_1) {
                lv_label_set_text(s_label_indicator_deck, "DECK 1");
            } else {
                lv_label_set_text(s_label_indicator_deck, "DECK 2");
            }
            lv_label_set_text(s_label_indicator_status, "READY");
            lv_obj_set_style_text_color(s_label_indicator_deck, COL_TEXT, LV_PART_MAIN);
            lv_obj_set_style_text_color(s_label_indicator_status, COL_TEXT_MUTED, LV_PART_MAIN);
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

esp_err_t ui_library_load_track_index_for_deck(int index, uint8_t deck)
{
#ifndef WIN32
    if (index < 0 || index >= media_catalog_count()) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!ui_library_try_begin_track_load()) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t rc = ui_submit_track_load(index, deck);
    if (rc != ESP_OK) {
        return rc;
    }
    return ESP_OK;
#else
    if (index < 0 || index >= library_count()) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!ui_library_try_begin_track_load()) {
        return ESP_ERR_INVALID_STATE;
    }
    mock_library_load_track_to_deck(index);
    library_track_t *track = library_get_ptr(index);
    if (track) {
        library_load_anlz(track);
        library_load_current_anlz(track);
        const anlz_metadata_t *meta = library_get_current_anlz();
        s_deck_loaded_track_key[deck] = track->track_id;
        s_deck_loaded_track_valid[deck] = true;
        if (s_library_table) {
            lv_obj_invalidate(s_library_table);
        }
        ui_library_apply_loaded_track(deck,
                                      track->title,
                                      track->artist,
                                      track->bpm,
                                      track->duration_ms,
                                      track->waveform_low,
                                      track->has_waveform != 0,
                                      meta);
    }
    ui_library_finish_track_load();
    return ESP_OK;
#endif
}

#endif
