#include "ui_overlay_map.h"

bool ui_overlay_map_ppa270(ui_overlay_rect_t logical,
                           int logical_w,
                           int logical_h,
                           ui_overlay_rect_t *physical)
{
    if (!physical || logical_w <= 0 || logical_h <= 0 ||
        logical.w <= 0 || logical.h <= 0 ||
        logical.x < 0 || logical.y < 0 ||
        logical.x + logical.w > logical_w ||
        logical.y + logical.h > logical_h) {
        return false;
    }

    physical->x = logical_h - (logical.y + logical.h);
    physical->y = logical.x;
    physical->w = logical.h;
    physical->h = logical.w;
    return true;
}

void ui_overlay_i8_to_rgb565(const uint8_t *src,
                             int src_stride_px,
                             uint16_t *dst,
                             int dst_stride_px,
                             int width_px,
                             int height_px,
                             const uint16_t *palette,
                             size_t palette_count)
{
    if (!src || !dst || !palette || src_stride_px <= 0 || dst_stride_px <= 0 ||
        width_px <= 0 || height_px <= 0 || palette_count == 0) {
        return;
    }

    for (int y = 0; y < height_px; y++) {
        const uint8_t *src_row = src + (size_t)y * (size_t)src_stride_px;
        uint16_t *dst_row = dst + (size_t)y * (size_t)dst_stride_px;
        for (int x = 0; x < width_px; x++) {
            uint8_t index = src_row[x];
            dst_row[x] = (index < palette_count) ? palette[index] : palette[0];
        }
    }
}
