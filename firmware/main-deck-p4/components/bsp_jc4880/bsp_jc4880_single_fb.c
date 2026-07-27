/*
 * Honest single-framebuffer display configuration.
 *
 * The previous DPI configuration allocated three full framebuffers, while the
 * UI backend always rendered into framebuffer zero and never issued a panel
 * draw/swap for framebuffer one or two. Production therefore behaved as a
 * single-buffer renderer while reserving roughly 1.54 MiB of unused PSRAM.
 */
#define bsp_display_init bsp_display_init_legacy_triple_buffer
#include "bsp_jc4880.c"
#undef bsp_display_init

esp_err_t bsp_display_init(void)
{
    esp_ldo_channel_config_t ldo_cfg = {
        .chan_id = BSP_MIPI_LDO_CHAN,
        .voltage_mv = BSP_MIPI_LDO_MV,
    };
    ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo_cfg, &s_mipi_ldo));
    ESP_LOGI(TAG, "MIPI DSI PHY powered (LDO VO%d @ %d mV)",
             BSP_MIPI_LDO_CHAN, BSP_MIPI_LDO_MV);

    esp_lcd_dsi_bus_handle_t dsi_bus = NULL;
    esp_lcd_dsi_bus_config_t bus_cfg = {
        .bus_id = 0,
        .num_data_lanes = BSP_DSI_LANE_NUM,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = BSP_DSI_LANE_MBPS,
    };
    ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bus_cfg, &dsi_bus));

    esp_lcd_panel_io_handle_t dbi_io = NULL;
    esp_lcd_dbi_io_config_t dbi_cfg = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(dsi_bus, &dbi_cfg, &dbi_io));

    esp_lcd_dpi_panel_config_t dpi_cfg = {
        .virtual_channel = 0,
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = BSP_DPI_CLK_MHZ,
        .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
        /* The current partial LVGL/PPA backend writes one scanned buffer. Do not
         * reserve two extra full-screen buffers until a real refresh-boundary
         * inactive-buffer swap is implemented and hardware-validated. */
        .num_fbs = 1,
        .video_timing = {
            .h_size = BSP_LCD_H_RES,
            .v_size = BSP_LCD_V_RES,
            .hsync_pulse_width = BSP_LCD_HSYNC,
            .hsync_back_porch = BSP_LCD_HBP,
            .hsync_front_porch = BSP_LCD_HFP,
            .vsync_pulse_width = BSP_LCD_VSYNC,
            .vsync_back_porch = BSP_LCD_VBP,
            .vsync_front_porch = BSP_LCD_VFP,
        },
        .flags.use_dma2d = false,
    };

    st7701_vendor_config_t vendor_cfg = {
        .init_cmds = s_st7701_init_cmds,
        .init_cmds_size = sizeof(s_st7701_init_cmds) / sizeof(s_st7701_init_cmds[0]),
        .mipi_config = {
            .dsi_bus = dsi_bus,
            .dpi_config = &dpi_cfg,
        },
        .flags = {
            .use_mipi_interface = 1,
        },
    };
    esp_lcd_panel_dev_config_t dev_cfg = {
        .reset_gpio_num = BSP_LCD_RST_GPIO,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor_cfg,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7701(dbi_io, &dev_cfg, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_LOGI(TAG, "ST7701S panel up (%dx%d, %d MHz DPI, RGB565, single framebuffer)",
             BSP_LCD_H_RES, BSP_LCD_V_RES, BSP_DPI_CLK_MHZ);

    ledc_timer_config_t bl_timer = {
        .speed_mode = BSP_BL_LEDC_MODE,
        .timer_num = BSP_BL_LEDC_TIMER,
        .duty_resolution = BSP_BL_LEDC_RES,
        .freq_hz = BSP_BL_LEDC_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&bl_timer));
    ledc_channel_config_t bl_chan = {
        .gpio_num = BSP_LCD_BL_GPIO,
        .speed_mode = BSP_BL_LEDC_MODE,
        .channel = BSP_BL_LEDC_CHANNEL,
        .timer_sel = BSP_BL_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&bl_chan));
    bsp_display_set_backlight(BSP_BL_DEFAULT_PCT);
    ESP_LOGI(TAG, "backlight PWM ready (GPIO%d, %d%%)",
             BSP_LCD_BL_GPIO, BSP_BL_DEFAULT_PCT);
    return ESP_OK;
}
