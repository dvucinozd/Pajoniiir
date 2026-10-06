#include "ui_color_preview.h"
#include <string.h>

bool ui_color_preview_draw(uint8_t *pixels, int stride,
                                        int width, int height,
                                        const anlz_metadata_t *meta,
                                        uint32_t duration_ms, uint32_t analysis_span_ms)
{
    if (!pixels || !meta || !meta->color_preview || width <= 0 ||
        height <= 0 || stride < width || !duration_ms || !analysis_span_ms) return false;
    uint8_t peak = anlz_color_preview_peak(meta->color_preview,
                                           meta->color_preview_len);
    if (!peak) return false;
    memset(pixels, 0, (size_t)stride * height);
    for (int x = 0; x < width; x += 2) {
        uint64_t analysis_x = (uint64_t)x * duration_ms / analysis_span_ms;
        if (analysis_x >= (uint32_t)width) continue;
        anlz_color_column_t col;
        if (!anlz_color_preview_column(meta->color_preview,
                                       meta->color_preview_len,
                                       (uint32_t)analysis_x, (uint32_t)width, &col))
            continue;
        int bar_height = (int)col.height * (height - 2) / peak;
        if (col.height && bar_height == 0) bar_height = 1;
        uint8_t color = 4; /* white if no band data */
        if (col.r || col.g || col.b) {
            if (col.r >= col.g && col.r >= col.b)
                color = col.g * 2u >= col.r ? 6 :
                        (col.b * 2u >= col.r ? 7 : 1);
            else if (col.g >= col.b)
                color = col.b * 2u >= col.g ? 3 : 5;
            else
                color = col.r * 2u >= col.b ? 7 :
                        (col.g * 2u >= col.b ? 3 : 2);
        }
        for (int y = height - bar_height; y < height; ++y)
            pixels[y * stride + x] = color;
    }
    return true;
}
