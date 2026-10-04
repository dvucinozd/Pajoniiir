#pragma once

#include "dj_ui.h"
#include "ui_frame_context.h"

/* LVGL owner only. Frame metadata is borrowed only for the duration of update;
 * draw callbacks see owned pixels, never a released ANLZ snapshot. */
void ui_dj_bridge_init(lv_obj_t *parent, const dj_ui_callbacks_t *callbacks,
                       bool ethernet, bool recorder);
void ui_dj_bridge_update(const ui_frame_context_t *frame);
void ui_dj_bridge_reset(void);
void ui_dj_bridge_zoom_delta(int delta);
typedef struct {
    uint32_t surface_bytes;
    uint32_t columns_rendered;
    uint32_t cache_full_updates;
} ui_dj_bridge_stats_t;
void ui_dj_bridge_get_stats(uint8_t deck, ui_dj_bridge_stats_t *out);
