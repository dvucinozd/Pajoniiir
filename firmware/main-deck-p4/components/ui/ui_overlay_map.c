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

    uint16_t lut[256];
    uint16_t fallback = palette[0];
    for (size_t i = 0; i < 256; i++) {
        lut[i] = (i < palette_count) ? palette[i] : fallback;
    }

    for (int y = 0; y < height_px; y++) {
        const uint8_t *src_row = src + (size_t)y * (size_t)src_stride_px;
        uint16_t *dst_row = dst + (size_t)y * (size_t)dst_stride_px;
        int x = 0;
        for (; x + 3 < width_px; x += 4) {
            dst_row[x] = lut[src_row[x]];
            dst_row[x + 1] = lut[src_row[x + 1]];
            dst_row[x + 2] = lut[src_row[x + 2]];
            dst_row[x + 3] = lut[src_row[x + 3]];
        }
        for (; x < width_px; x++) {
            dst_row[x] = lut[src_row[x]];
        }
    }
}
