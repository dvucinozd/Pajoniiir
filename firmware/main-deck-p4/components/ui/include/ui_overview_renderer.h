#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "rekordbox_anlz.h"
#include "ui_waveform_model.h"

#ifdef __cplusplus
extern "C" {
#endif

void ui_overview_renderer_draw_main(uint8_t *pixels,
                                    int stride_px,
                                    int width_px,
                                    int height_px,
                                    const ui_waveform_source_t *source,
                                    uint32_t duration_ms,
                                    const anlz_metadata_t *meta,
                                    uint32_t center_ms,
                                    uint32_t window_ms);

bool ui_overview_renderer_draw_mini(uint8_t *pixels,
                                    int stride_px,
                                    int width_px,
                                    int height_px,
                                    const ui_waveform_source_t *source,
                                    uint32_t duration_ms);

#ifdef __cplusplus
}
#endif
