#include "ui_lvgl_backend.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "lvgl.h"
#include "ui_diagnostics.h"
#include "ui_overview_perf.h"

static const char *TAG = "ui";

static esp_err_t ui_lvgl_backend_validate_rgb565_region_args(const ui_overlay_rect_t *logical,
                                                             const uint16_t *src,
                                                             uint32_t src_w,
                                                             uint32_t src_h,
                                                             uint32_t src_x,
                                                             uint32_t src_y,
                                                             uint32_t block_w,
                                                             uint32_t block_h,
                                                             size_t src_bytes)
{
    if (!logical || !src || logical->w <= 0 || logical->h <= 0 ||
        src_w == 0 || src_h == 0 || block_w == 0 || block_h == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (src_x >= src_w || src_y >= src_h ||
        block_w > src_w - src_x || block_h > src_h - src_y) {
        return ESP_ERR_INVALID_ARG;
    }
    if (src_w > ((size_t)-1) / sizeof(uint16_t) ||
        src_h > (((size_t)-1) / sizeof(uint16_t)) / src_w) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t required_bytes = (size_t)src_w * (size_t)src_h * sizeof(uint16_t);
    if (src_bytes < required_bytes) {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

#ifdef WIN32
esp_err_t ui_lvgl_backend_init(uint16_t hor_res, uint16_t ver_res)
{
    (void)hor_res;
    (void)ver_res;
    return ESP_OK;
}

esp_err_t ui_lvgl_backend_start(void)
{
    return ESP_OK;
}

void ui_lvgl_lock(void) {}
void ui_lvgl_unlock(void) {}

void *ui_lvgl_backend_alloc_dma_buffer(size_t bytes, size_t *aligned_bytes)
{
    if (aligned_bytes) {
        *aligned_bytes = bytes;
    }
    return malloc(bytes);
}

esp_err_t ui_lvgl_backend_blit_rgb565_ppa270(const ui_overlay_rect_t *logical,
                                             const uint16_t *src,
                                             uint32_t src_w,
                                             uint32_t src_h,
                                             size_t src_bytes,
                                             ui_lvgl_backend_blit_perf_t *perf)
{
    return ui_lvgl_backend_blit_rgb565_ppa270_region(logical,
                                                     src,
                                                     src_w,
                                                     src_h,
                                                     0,
                                                     0,
                                                     src_w,
                                                     src_h,
                                                     src_bytes,
                                                     perf);
}

esp_err_t ui_lvgl_backend_blit_rgb565_ppa270_region(const ui_overlay_rect_t *logical,
                                                    const uint16_t *src,
                                                    uint32_t src_w,
                                                    uint32_t src_h,
                                                    uint32_t src_x,
                                                    uint32_t src_y,
                                                    uint32_t block_w,
                                                    uint32_t block_h,
                                                    size_t src_bytes,
                                                    ui_lvgl_backend_blit_perf_t *perf)
{
    esp_err_t err = ui_lvgl_backend_validate_rgb565_region_args(logical,
                                                                src,
                                                                src_w,
                                                                src_h,
                                                                src_x,
                                                                src_y,
                                                                block_w,
                                                                block_h,
                                                                src_bytes);
    if (err != ESP_OK) {
        return err;
    }
    if (perf) {
        memset(perf, 0, sizeof(*perf));
    }
    return ESP_ERR_NOT_SUPPORTED;
}
#else
#include "bsp_jc4880.h"
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_private/esp_cache_private.h"
#include "esp_timer.h"
#include "driver/ppa.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <sys/lock.h>

#define LVGL_TICK_PERIOD_MS        2
#define LVGL_TASK_STACK            (24 * 1024)
#define LVGL_TASK_PRIO             2
#define UI_DSI_FB_COUNT            3
#define UI_LVGL_PARTIAL_BUF_ROWS   80u
#define UI_PERF_SPIKE_THRESHOLD_US 20000u
#define ALIGN_UP_BY(n, a)          (((n) + ((a) - 1)) & ~((a) - 1))

static _lock_t s_lvgl_lock;
static lv_display_t *s_disp = NULL;
static ppa_client_handle_t s_ppa = NULL;
static void *s_dsi_fb[UI_DSI_FB_COUNT] = { NULL };
static int s_dsi_active_fb_idx = 0;
static size_t s_cache_align = 64;
static uint16_t s_hor_res = 800;
static uint16_t s_ver_res = 480;

static ui_overview_perf_counter_t s_lvgl_handler_interval_perf;
static ui_overview_perf_counter_t s_lvgl_handler_duration_perf;
static ui_overview_perf_counter_t s_lvgl_flush_total_perf;
static ui_overview_perf_counter_t s_lvgl_flush_msync_perf;
static ui_overview_perf_counter_t s_lvgl_flush_ppa_perf;
static ui_overview_perf_counter_t s_lvgl_refr_total_perf;
static ui_overview_perf_counter_t s_lvgl_render_total_perf;
static ui_overview_perf_counter_t s_lvgl_flush_event_perf;

static int64_t s_lvgl_refr_start_us = 0;
static int64_t s_lvgl_render_start_us = 0;
static int64_t s_lvgl_flush_event_start_us = 0;
static uint32_t s_lvgl_inval_count = 0;
static uint32_t s_lvgl_inval_total_px = 0;
static uint32_t s_lvgl_inval_max_px = 0;
static uint32_t s_lvgl_frame_inval_count = 0;
static uint32_t s_lvgl_frame_inval_total_px = 0;
static uint32_t s_lvgl_frame_inval_max_px = 0;

static void ui_lvgl_backend_perf_log_us(const char *label, const ui_overview_perf_report_t *report)
{
    if (!label || !report) {
        return;
    }

    ESP_LOGI(TAG, "%s: last=%u us avg=%u us max=%u us samples=%u",
             label,
             (unsigned)report->last_us,
             (unsigned)report->avg_us,
             (unsigned)report->max_us,
             (unsigned)report->samples);
}

static uint32_t ui_lvgl_backend_perf_elapsed_us(int64_t start_us)
{
    int64_t elapsed_us = esp_timer_get_time() - start_us;
    return elapsed_us > 0 ? (uint32_t)elapsed_us : 0u;
}

static bool ui_lvgl_backend_perf_record_phase_us(ui_overview_perf_counter_t *counter,
                                                 const char *label,
                                                 uint32_t duration_us)
{
    if (!ui_diagnostics_enabled()) {
        return false;
    }

    ui_overview_perf_report_t report;
    if (ui_overview_perf_record(counter, duration_us, &report)) {
        ui_lvgl_backend_perf_log_us(label, &report);
        if (report.max_us >= UI_PERF_SPIKE_THRESHOLD_US) {
            ESP_LOGW(TAG,
                     "%s spike window max: %u us",
                     label,
                     (unsigned)report.max_us);
            return true;
        }
    }
    return false;
}

static void ui_lvgl_log_frame_context(const char *label)
{
    ESP_LOGI(TAG,
             "%s invalidated: count=%u total_px=%u max_px=%u",
             label,
             (unsigned)s_lvgl_frame_inval_count,
             (unsigned)s_lvgl_frame_inval_total_px,
             (unsigned)s_lvgl_frame_inval_max_px);
}

static void ui_lvgl_display_event_cb(lv_event_t *e)
{
    if (!ui_diagnostics_enabled()) {
        return;
    }

    lv_event_code_t code = lv_event_get_code(e);

    switch (code) {
    case LV_EVENT_INVALIDATE_AREA: {
        lv_area_t *area = lv_event_get_invalidated_area(e);
        if (!area) {
            break;
        }
        int32_t w = area->x2 - area->x1 + 1;
        int32_t h = area->y2 - area->y1 + 1;
        if (w <= 0 || h <= 0) {
            break;
        }
        uint32_t px = (uint32_t)w * (uint32_t)h;
        s_lvgl_inval_count++;
        s_lvgl_inval_total_px += px;
        if (px > s_lvgl_inval_max_px) {
            s_lvgl_inval_max_px = px;
        }
        break;
    }
    case LV_EVENT_REFR_START:
        s_lvgl_refr_start_us = esp_timer_get_time();
        s_lvgl_frame_inval_count = s_lvgl_inval_count;
        s_lvgl_frame_inval_total_px = s_lvgl_inval_total_px;
        s_lvgl_frame_inval_max_px = s_lvgl_inval_max_px;
        s_lvgl_inval_count = 0;
        s_lvgl_inval_total_px = 0;
        s_lvgl_inval_max_px = 0;
        break;
    case LV_EVENT_REFR_READY:
        if (s_lvgl_refr_start_us != 0) {
            uint32_t elapsed_us = ui_lvgl_backend_perf_elapsed_us(s_lvgl_refr_start_us);
            if (ui_lvgl_backend_perf_record_phase_us(&s_lvgl_refr_total_perf,
                                                     "LVGL refr total",
                                                     elapsed_us)) {
                ui_lvgl_log_frame_context("LVGL refr");
            }
        }
        break;
    case LV_EVENT_RENDER_START:
        s_lvgl_render_start_us = esp_timer_get_time();
        break;
    case LV_EVENT_RENDER_READY:
        if (s_lvgl_render_start_us != 0) {
            uint32_t elapsed_us = ui_lvgl_backend_perf_elapsed_us(s_lvgl_render_start_us);
            if (ui_lvgl_backend_perf_record_phase_us(&s_lvgl_render_total_perf,
                                                     "LVGL render total",
                                                     elapsed_us)) {
                ui_lvgl_log_frame_context("LVGL render");
            }
        }
        break;
    case LV_EVENT_FLUSH_START:
        s_lvgl_flush_event_start_us = esp_timer_get_time();
        break;
    case LV_EVENT_FLUSH_FINISH:
        if (s_lvgl_flush_event_start_us != 0) {
            ui_lvgl_backend_perf_record_phase_us(&s_lvgl_flush_event_perf,
                                                 "LVGL flush event",
                                                 ui_lvgl_backend_perf_elapsed_us(s_lvgl_flush_event_start_us));
        }
        break;
    default:
        break;
    }
}

static esp_err_t ui_lvgl_backend_blit_rgb565_ppa270_mapped(const ui_overlay_rect_t *logical,
                                                           const ui_overlay_rect_t *physical,
                                                           const uint16_t *src,
                                                           uint32_t src_w,
                                                           uint32_t src_h,
                                                           uint32_t src_x,
                                                           uint32_t src_y,
                                                           uint32_t block_w,
                                                           uint32_t block_h,
                                                           size_t src_bytes,
                                                           ui_lvgl_backend_blit_perf_t *perf)
{
    if (perf) {
        memset(perf, 0, sizeof(*perf));
    }
    esp_err_t arg_err = ui_lvgl_backend_validate_rgb565_region_args(logical,
                                                                    src,
                                                                    src_w,
                                                                    src_h,
                                                                    src_x,
                                                                    src_y,
                                                                    block_w,
                                                                    block_h,
                                                                    src_bytes);
    if (arg_err != ESP_OK || !physical || physical->w <= 0 || physical->h <= 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ppa || s_dsi_active_fb_idx < 0 || s_dsi_active_fb_idx >= UI_DSI_FB_COUNT ||
        !s_dsi_fb[s_dsi_active_fb_idx]) {
        return ESP_ERR_INVALID_STATE;
    }

    int64_t total_start_us = esp_timer_get_time();
    int64_t msync_start_us = esp_timer_get_time();
    esp_cache_msync((void *)src,
                    src_bytes,
                    ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
    uint32_t msync_us = ui_lvgl_backend_perf_elapsed_us(msync_start_us);

    ppa_srm_oper_config_t op = {
        .in.buffer          = (void *)src,
        .in.pic_w           = src_w,
        .in.pic_h           = src_h,
        .in.block_w         = block_w,
        .in.block_h         = block_h,
        .in.block_offset_x  = src_x,
        .in.block_offset_y  = src_y,
        .in.srm_cm          = PPA_SRM_COLOR_MODE_RGB565,

        .out.buffer         = s_dsi_fb[s_dsi_active_fb_idx],
        .out.buffer_size    = ALIGN_UP_BY((size_t)BSP_LCD_H_RES * BSP_LCD_V_RES * 2,
                                          s_cache_align),
        .out.pic_w          = BSP_LCD_H_RES,
        .out.pic_h          = BSP_LCD_V_RES,
        .out.block_offset_x = (uint32_t)physical->x,
        .out.block_offset_y = (uint32_t)physical->y,
        .out.srm_cm         = PPA_SRM_COLOR_MODE_RGB565,

        .rotation_angle     = PPA_SRM_ROTATION_ANGLE_270,
        .scale_x            = 1.0,
        .scale_y            = 1.0,
        .rgb_swap           = 0,
        .byte_swap          = 0,
        .mode               = PPA_TRANS_MODE_BLOCKING,
    };

    int64_t ppa_start_us = esp_timer_get_time();
    esp_err_t err = ppa_do_scale_rotate_mirror(s_ppa, &op);
    uint32_t ppa_us = ui_lvgl_backend_perf_elapsed_us(ppa_start_us);
    uint32_t total_us = ui_lvgl_backend_perf_elapsed_us(total_start_us);

    if (perf) {
        perf->msync_us = msync_us;
        perf->ppa_us = ppa_us;
        perf->total_us = total_us;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG,
                 "RGB565 PPA270 blit failed: %s logical=(%d,%d %dx%d) physical=(%d,%d %dx%d)",
                 esp_err_to_name(err),
                 logical->x, logical->y, logical->w, logical->h,
                 physical->x, physical->y, physical->w, physical->h);
    }
    return err;
}

static void ui_lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    if (!area || !px_map) {
        lv_display_flush_ready(disp);
        return;
    }

    int32_t area_w = area->x2 - area->x1 + 1;
    int32_t area_h = area->y2 - area->y1 + 1;
    if (area_w <= 0 || area_h <= 0) {
        lv_display_flush_ready(disp);
        return;
    }

    ui_overlay_rect_t logical = {
        .x = area->x1,
        .y = area->y1,
        .w = area_w,
        .h = area_h,
    };
    ui_overlay_rect_t physical;
    if (!ui_overlay_map_ppa270(logical, s_hor_res, s_ver_res, &physical)) {
        ESP_LOGW(TAG,
                 "LVGL flush area outside canvas: x=%d y=%d w=%d h=%d",
                 logical.x, logical.y, logical.w, logical.h);
        lv_display_flush_ready(disp);
        return;
    }

    ui_lvgl_backend_blit_perf_t perf = {0};
    esp_err_t err = ui_lvgl_backend_blit_rgb565_ppa270_mapped(&logical,
                                                              &physical,
                                                              (const uint16_t *)px_map,
                                                              (uint32_t)area_w,
                                                              (uint32_t)area_h,
                                                              0,
                                                              0,
                                                              (uint32_t)area_w,
                                                              (uint32_t)area_h,
                                                              (size_t)area_w * (size_t)area_h * sizeof(uint16_t),
                                                              &perf);
    ui_lvgl_backend_perf_record_phase_us(&s_lvgl_flush_msync_perf,
                                         "LVGL flush msync",
                                         perf.msync_us);
    ui_lvgl_backend_perf_record_phase_us(&s_lvgl_flush_ppa_perf,
                                         "LVGL flush PPA",
                                         perf.ppa_us);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "LVGL flush PPA failed: %s", esp_err_to_name(err));
    }
    ui_lvgl_backend_perf_record_phase_us(&s_lvgl_flush_total_perf,
                                         "LVGL flush total",
                                         perf.total_us);
    lv_display_flush_ready(disp);
}

static void ui_lvgl_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

static void ui_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    esp_lcd_touch_handle_t tp = lv_indev_get_user_data(indev);
    if (tp == NULL) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    esp_lcd_touch_point_data_t point = {0};
    uint8_t cnt = 0;
    esp_lcd_touch_read_data(tp);
    esp_err_t rc = esp_lcd_touch_get_data(tp, &point, &cnt, 1);
    if (rc == ESP_OK && cnt > 0) {
        data->point.x = point.x;
        data->point.y = point.y;
        data->state   = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static void ui_lvgl_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "LVGL handler task started");
    uint64_t last_handler_start_us = 0;
    while (1) {
        uint64_t handler_start_us = (uint64_t)esp_timer_get_time();
        if (ui_diagnostics_enabled() && last_handler_start_us != 0) {
            ui_overview_perf_report_t interval_report;
            if (ui_overview_perf_record(&s_lvgl_handler_interval_perf,
                                        (uint32_t)(handler_start_us - last_handler_start_us),
                                        &interval_report)) {
                ui_lvgl_backend_perf_log_us("LVGL handler interval", &interval_report);
            }
        }
        last_handler_start_us = handler_start_us;

        _lock_acquire_recursive(&s_lvgl_lock);
        uint32_t next_ms = lv_timer_handler();
        _lock_release_recursive(&s_lvgl_lock);

        uint64_t handler_end_us = (uint64_t)esp_timer_get_time();
        if (ui_diagnostics_enabled()) {
            ui_overview_perf_report_t duration_report;
            if (ui_overview_perf_record(&s_lvgl_handler_duration_perf,
                                        (uint32_t)(handler_end_us - handler_start_us),
                                        &duration_report)) {
                ui_lvgl_backend_perf_log_us("LVGL handler duration", &duration_report);
            }
        }

        if (next_ms > 100) next_ms = 100;
        if (next_ms < 5)   next_ms = 5;
        vTaskDelay(pdMS_TO_TICKS(next_ms));
    }
}

esp_err_t ui_lvgl_backend_init(uint16_t hor_res, uint16_t ver_res)
{
    if (hor_res == 0 || ver_res == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    s_hor_res = hor_res;
    s_ver_res = ver_res;
    _lock_init_recursive(&s_lvgl_lock);

    esp_lcd_panel_handle_t panel = bsp_display_get_panel_handle();
    if (panel == NULL) {
        ESP_LOGE(TAG, "panel handle is NULL - call bsp_display_init() first");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_ERROR_CHECK(esp_lcd_dpi_panel_get_frame_buffer(panel, UI_DSI_FB_COUNT,
                                                       &s_dsi_fb[0], &s_dsi_fb[1], &s_dsi_fb[2]));
    ESP_ERROR_CHECK(esp_cache_get_alignment(MALLOC_CAP_DMA | MALLOC_CAP_SPIRAM, &s_cache_align));

    ppa_client_config_t ppa_cfg = { .oper_type = PPA_OPERATION_SRM };
    ESP_ERROR_CHECK(ppa_register_client(&ppa_cfg, &s_ppa));

    lv_init();

    s_disp = lv_display_create(s_hor_res, s_ver_res);
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_user_data(s_disp, panel);
    lv_display_set_flush_cb(s_disp, ui_lvgl_flush_cb);
    lv_display_add_event_cb(s_disp, ui_lvgl_display_event_cb, LV_EVENT_ALL, NULL);

    size_t buf_sz = ALIGN_UP_BY((size_t)s_hor_res *
                                UI_LVGL_PARTIAL_BUF_ROWS *
                                sizeof(uint16_t),
                                s_cache_align);
    void *buf1 = heap_caps_aligned_alloc(s_cache_align, buf_sz, MALLOC_CAP_SPIRAM);
    if (!buf1) {
        ESP_LOGE(TAG, "failed to allocate %u-byte LVGL draw buffer from PSRAM", (unsigned)buf_sz);
        return ESP_ERR_NO_MEM;
    }
    lv_display_set_buffers(s_disp, buf1, NULL, buf_sz, LV_DISPLAY_RENDER_MODE_PARTIAL);

    const esp_timer_create_args_t tick_args = {
        .callback = ui_lvgl_tick_cb,
        .name     = "lvgl_tick",
    };
    esp_timer_handle_t tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, LVGL_TICK_PERIOD_MS * 1000));

    esp_lcd_touch_handle_t tp = bsp_touch_get_handle();
    if (tp != NULL) {
        lv_indev_t *indev = lv_indev_create();
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_user_data(indev, tp);
        lv_indev_set_read_cb(indev, ui_touch_read_cb);
        ESP_LOGI(TAG, "GT911 registered as LVGL pointer input");
    } else {
        ESP_LOGW(TAG, "no touch handle - UI will be display-only");
    }

    ESP_LOGI(TAG, "LVGL backend ready (%ux%u canvas -> PPA-rotated to %dx%d, RGB565)",
             (unsigned)s_hor_res,
             (unsigned)s_ver_res,
             BSP_LCD_H_RES,
             BSP_LCD_V_RES);
    return ESP_OK;
}

esp_err_t ui_lvgl_backend_start(void)
{
    if (xTaskCreate(ui_lvgl_task, "lvgl", LVGL_TASK_STACK, NULL, LVGL_TASK_PRIO, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void ui_lvgl_lock(void)
{
    _lock_acquire_recursive(&s_lvgl_lock);
}

void ui_lvgl_unlock(void)
{
    _lock_release_recursive(&s_lvgl_lock);
}

void *ui_lvgl_backend_alloc_dma_buffer(size_t bytes, size_t *aligned_bytes)
{
    if (bytes == 0) {
        if (aligned_bytes) {
            *aligned_bytes = 0;
        }
        return NULL;
    }

    size_t aligned = ALIGN_UP_BY(bytes, s_cache_align);
    if (aligned_bytes) {
        *aligned_bytes = aligned;
    }

    void *buf = heap_caps_aligned_alloc(s_cache_align,
                                        aligned,
                                        MALLOC_CAP_INTERNAL |
                                            MALLOC_CAP_DMA |
                                            MALLOC_CAP_8BIT);
    if (buf) {
        return buf;
    }
    buf = heap_caps_aligned_alloc(s_cache_align,
                                  aligned,
                                  MALLOC_CAP_SPIRAM |
                                      MALLOC_CAP_DMA |
                                      MALLOC_CAP_8BIT);
    if (buf) {
        return buf;
    }
    return heap_caps_aligned_alloc(s_cache_align,
                                   aligned,
                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

esp_err_t ui_lvgl_backend_blit_rgb565_ppa270(const ui_overlay_rect_t *logical,
                                             const uint16_t *src,
                                             uint32_t src_w,
                                             uint32_t src_h,
                                             size_t src_bytes,
                                             ui_lvgl_backend_blit_perf_t *perf)
{
    return ui_lvgl_backend_blit_rgb565_ppa270_region(logical,
                                                     src,
                                                     src_w,
                                                     src_h,
                                                     0,
                                                     0,
                                                     src_w,
                                                     src_h,
                                                     src_bytes,
                                                     perf);
}

esp_err_t ui_lvgl_backend_blit_rgb565_ppa270_region(const ui_overlay_rect_t *logical,
                                                    const uint16_t *src,
                                                    uint32_t src_w,
                                                    uint32_t src_h,
                                                    uint32_t src_x,
                                                    uint32_t src_y,
                                                    uint32_t block_w,
                                                    uint32_t block_h,
                                                    size_t src_bytes,
                                                    ui_lvgl_backend_blit_perf_t *perf)
{
    if (perf) {
        memset(perf, 0, sizeof(*perf));
    }
    esp_err_t arg_err = ui_lvgl_backend_validate_rgb565_region_args(logical,
                                                                    src,
                                                                    src_w,
                                                                    src_h,
                                                                    src_x,
                                                                    src_y,
                                                                    block_w,
                                                                    block_h,
                                                                    src_bytes);
    if (arg_err != ESP_OK) {
        return arg_err;
    }

    ui_overlay_rect_t physical;
    if (!ui_overlay_map_ppa270(*logical, s_hor_res, s_ver_res, &physical)) {
        return ESP_ERR_INVALID_ARG;
    }

    return ui_lvgl_backend_blit_rgb565_ppa270_mapped(logical,
                                                     &physical,
                                                     src,
                                                     src_w,
                                                     src_h,
                                                     src_x,
                                                     src_y,
                                                     block_w,
                                                     block_h,
                                                     src_bytes,
                                                     perf);
}
#endif
