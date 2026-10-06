#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// P4 rev < 3.0 shares the DPI input/output format field. Keep memory RGB888
// when sending RGB888 on DSI. Packed little-endian pixels are B, G, R bytes;
// do not use sizeof(color_pixel_rgb888_data_t), whose union includes uint32_t.
#define BSP_SCANOUT_BYTES_PER_PIXEL 3u

static inline void bsp_scanout_store_rgb565(uint8_t *dst, uint16_t color)
{
    unsigned b = color & 31u;
    unsigned g = (color >> 5) & 63u;
    unsigned r = (color >> 11) & 31u;
    dst[0] = (uint8_t)((b << 3) | (b >> 2));
    dst[1] = (uint8_t)((g << 2) | (g >> 4));
    dst[2] = (uint8_t)((r << 3) | (r >> 2));
}

static inline bool bsp_scanout_fill_rect_rgb565(uint8_t *fb, size_t bytes,
                                               unsigned width, unsigned height,
                                               unsigned x, unsigned y,
                                               unsigned w, unsigned h, uint16_t color)
{
    if (!fb || !width || !height || !w || !h || x >= width || y >= height ||
        w > width - x || h > height - y) return false;
    size_t stride = (size_t)width * BSP_SCANOUT_BYTES_PER_PIXEL;
    if (stride / BSP_SCANOUT_BYTES_PER_PIXEL != width ||
        (size_t)height > SIZE_MAX / stride || bytes < stride * height) return false;
    for (unsigned row = y; row < y + h; ++row) {
        uint8_t *dst = fb + (size_t)row * stride + (size_t)x * BSP_SCANOUT_BYTES_PER_PIXEL;
        for (unsigned col = 0; col < w; ++col) {
            bsp_scanout_store_rgb565(dst, color);
            dst += BSP_SCANOUT_BYTES_PER_PIXEL;
        }
    }
    return true;
}
