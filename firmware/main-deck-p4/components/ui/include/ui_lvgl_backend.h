#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "ui_overlay_map.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t msync_us;
    uint32_t ppa_us;
    uint32_t total_us;
} ui_lvgl_backend_blit_perf_t;

typedef void (*ui_lvgl_backend_frame_cb_t)(void *user_ctx);

esp_err_t ui_lvgl_backend_init(uint16_t hor_res, uint16_t ver_res);
// Runs callback from the LVGL task on each delivered panel-refresh event.
// Register it before ui_lvgl_backend_start(). Refreshes that arrive while the
// task is busy are coalesced so stale UI work cannot build up.
esp_err_t ui_lvgl_backend_set_frame_callback(ui_lvgl_backend_frame_cb_t callback,
                                             void *user_ctx);
esp_err_t ui_lvgl_backend_start(void);
void ui_lvgl_lock(void);
void ui_lvgl_unlock(void);

void *ui_lvgl_backend_alloc_dma_buffer(size_t bytes, size_t *aligned_bytes);

esp_err_t ui_lvgl_backend_blit_rgb565_ppa270(const ui_overlay_rect_t *logical,
                                             const uint16_t *src,
                                             uint32_t src_w,
                                             uint32_t src_h,
                                             size_t src_bytes,
                                             ui_lvgl_backend_blit_perf_t *perf);

esp_err_t ui_lvgl_backend_blit_rgb565_ppa270_region(const ui_overlay_rect_t *logical,
                                                    const uint16_t *src,
                                                    uint32_t src_w,
                                                    uint32_t src_h,
                                                    uint32_t src_x,
                                                    uint32_t src_y,
                                                    uint32_t block_w,
                                                    uint32_t block_h,
                                                    size_t src_bytes,
                                                    ui_lvgl_backend_blit_perf_t *perf);

esp_err_t ui_lvgl_backend_draw_rect_rgb565(const ui_overlay_rect_t *logical, uint16_t color);

#ifdef __cplusplus
}
#endif
