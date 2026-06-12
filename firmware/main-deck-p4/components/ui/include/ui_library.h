#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef UI_LIBRARY_TITLE_TEXT_MAX
#define UI_LIBRARY_TITLE_TEXT_MAX 128
#endif

#ifndef UI_LIBRARY_ARTIST_TEXT_MAX
#define UI_LIBRARY_ARTIST_TEXT_MAX 128
#endif

typedef struct {
    char title[UI_LIBRARY_TITLE_TEXT_MAX];
    char artist[UI_LIBRARY_ARTIST_TEXT_MAX];
    char bpm[16];
    char duration[16];
} ui_library_row_text_t;

typedef struct {
    bool apply_usb_removed;
    bool poll_track_load_result;
    bool refresh_library;
    bool focus_library_table;
} ui_library_update_plan_t;

void ui_library_format_row_text(ui_library_row_text_t *out,
                                const char *title,
                                const char *artist,
                                uint16_t bpm,
                                uint32_t duration_ms);

ui_library_update_plan_t ui_library_plan_update(int active_tab,
                                                bool needs_refresh,
                                                bool usb_removed_pending);

#ifndef UI_LIBRARY_HOST_TEST

#include "deck_core.h"
#include "esp_err.h"
#include "lvgl.h"
#include "rekordbox_anlz.h"
#include "ui_frame_context.h"

typedef struct {
    lv_style_t *screen_bg;
    lv_style_t *btn_primary;
    lv_style_t *btn_secondary;
    lv_style_t *btn_disabled;
    lv_style_t *pressed;
} ui_library_styles_t;

typedef struct {
    void (*status_hold)(const char *text, lv_color_t color, uint32_t hold_ms);
    lv_color_t (*status_color_for_text)(const char *status);
    void (*cache_invalidate)(void);
    void (*set_header_track)(const char *title, const char *artist, uint16_t bpm);
    void (*clear_deck_track_info)(uint8_t deck);
    void (*set_deck_track_info)(uint8_t deck,
                                const char *title,
                                const char *artist,
                                uint16_t bpm,
                                uint32_t duration_ms);
    void (*set_deck_anlz)(uint8_t deck, const anlz_metadata_t *meta);
    const anlz_metadata_t *(*get_deck_anlz)(uint8_t deck);
    void (*load_waveform_data)(uint8_t deck,
                               uint32_t duration_ms,
                               const uint8_t waveform_low[400],
                               bool has_waveform,
                               const anlz_metadata_t *meta);
    void (*set_loop_shadow)(uint8_t deck,
                            bool active,
                            uint32_t start_ms,
                            uint32_t end_ms,
                            int beats);
    bool (*is_performance_target_active)(uint8_t deck);
    void (*update_hot_cues)(void);
    void (*update_loop_screen_state)(void);
} ui_library_actions_t;

typedef struct {
    ui_library_styles_t styles;
    ui_library_actions_t actions;
    int hor_res;
    int content_y;
    int content_h;
} ui_library_config_t;

void ui_library_init(const ui_library_config_t *config);
lv_obj_t *ui_library_create(lv_obj_t *parent);
void ui_library_load_initial_track(void);
void ui_trigger_library_refresh(void);
void ui_refresh_library(void);
void ui_notify_usb_removed(void);
bool ui_is_library_active(void);
esp_err_t ui_library_select_delta(int delta);
esp_err_t ui_library_load_selected(void);
esp_err_t ui_library_load_selected_for_deck(uint8_t deck);
void ui_library_update(const ui_frame_context_t *ctx);
uint32_t ui_library_deck_duration_ms(uint8_t deck, uint32_t fallback_duration_ms);
uint16_t ui_library_deck_bpm(uint8_t deck, uint16_t fallback_bpm);
bool ui_library_has_remote_loaded_track(void);
bool ui_library_get_loaded_waveform(uint8_t deck,
                                    const uint8_t **waveform_low,
                                    bool *has_waveform);

#endif
