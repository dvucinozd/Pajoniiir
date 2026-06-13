#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "deck_core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_STATUS_TRANSPORT_PAUSE = 0,
    UI_STATUS_TRANSPORT_PLAY,
    UI_STATUS_TRANSPORT_LOADING,
} ui_status_transport_kind_t;

typedef struct {
    char text[16];
    ui_status_transport_kind_t kind;
} ui_status_transport_text_t;

void ui_status_format_transport_text(ui_status_transport_text_t *out,
                                     uint8_t active_deck,
                                     const deck_state_t *state,
                                     bool loading,
                                     uint8_t load_pct);

#ifndef UI_STATUS_HOST_TEST

#include "lvgl.h"
#include "ui_frame_context.h"

typedef struct {
    lv_obj_t *title;
    lv_obj_t *artist;
    lv_obj_t *time_elapsed;
    lv_obj_t *time_remain;
    lv_obj_t *bpm;
    lv_obj_t *pitch;
    lv_obj_t *status_indicator;
} ui_status_widgets_t;

void ui_status_init(const ui_status_widgets_t *widgets);
void ui_status_invalidate(void);
void ui_status_invalidate_header(void);
void ui_status_hold(const char *text, lv_color_t color, uint32_t hold_ms);
lv_color_t ui_status_color_for_text(const char *status);
void ui_status_set_header_track(const char *title, const char *artist, uint16_t bpm);
void ui_status_update(const ui_frame_context_t *ctx);

#endif

#ifdef __cplusplus
}
#endif
