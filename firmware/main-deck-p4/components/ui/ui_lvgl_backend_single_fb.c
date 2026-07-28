/* Production backend init matching the BSP's one real DPI framebuffer. */
#define ui_lvgl_backend_init ui_lvgl_backend_init_legacy_three_fb
#include "ui_lvgl_backend.c"
#undef ui_lvgl_backend_init

_Static_assert(BSP_LCD_FRAMEBUFFER_COUNT == 1u,
               "current LVGL backend supports exactly one DPI framebuffer");

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

    s_dsi_fb[0] = NULL;
    s_dsi_fb[1] = NULL;
    s_dsi_fb[2] = NULL;
    s_dsi_active_fb_idx = 0;
    ESP_ERROR_CHECK(esp_lcd_dpi_panel_get_frame_buffer(
        panel, BSP_LCD_FRAMEBUFFER_COUNT, &s_dsi_fb[0]));
    ESP_ERROR_CHECK(esp_cache_get_alignment(MALLOC_CAP_DMA | MALLOC_CAP_SPIRAM,
                                             &s_cache_align));

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
    void *buf1 = heap_caps_aligned_alloc(s_cache_align, buf_sz,
                                         MALLOC_CAP_SPIRAM);
    if (!buf1) {
        ESP_LOGE(TAG, "failed to allocate %u-byte LVGL draw buffer from PSRAM",
                 (unsigned)buf_sz);
        return ESP_ERR_NO_MEM;
    }
    lv_display_set_buffers(s_disp, buf1, NULL, buf_sz,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    const esp_timer_create_args_t tick_args = {
        .callback = ui_lvgl_tick_cb,
        .name = "lvgl_tick",
    };
    esp_timer_handle_t tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(
        tick_timer, LVGL_TICK_PERIOD_MS * 1000));

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

    const esp_lcd_dpi_panel_event_callbacks_t panel_cbs = {
        .on_refresh_done = ui_lvgl_dpi_refresh_done_cb,
    };
    esp_err_t panel_cb_rc = esp_lcd_dpi_panel_register_event_callbacks(
        panel, &panel_cbs, NULL);
    if (panel_cb_rc != ESP_OK) {
        ESP_LOGE(TAG, "failed to register DPI refresh callback: %s",
                 esp_err_to_name(panel_cb_rc));
        return panel_cb_rc;
    }

    ESP_LOGI(TAG,
             "LVGL backend ready (%ux%u canvas -> PPA-rotated to %dx%d, RGB565, one DPI framebuffer)",
             (unsigned)s_hor_res,
             (unsigned)s_ver_res,
             BSP_LCD_H_RES,
             BSP_LCD_V_RES);
    return ESP_OK;
}
