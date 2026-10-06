#pragma once
#include "rekordbox_anlz.h"

bool ui_color_preview_draw(uint8_t *pixels, int stride, int width, int height,
                            const anlz_metadata_t *meta, uint32_t duration_ms,
                            uint32_t analysis_span_ms);
