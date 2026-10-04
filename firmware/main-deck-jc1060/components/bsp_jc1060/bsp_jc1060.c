/* SPDX-License-Identifier: MIT
 * JD9165 register sequence/timing adapted from kayrozen/Pajoniiir
 * 428b97dd4a175f03d3a172c8db9c4d5ed94195fb, bsp_jc1060p470/src/bsp_display.c.
 * Copyright (c) 2024 The Pajoniiir Contributors.
 * Physical panel revision/colors/timing: NOT RUN.
 */
#include "board_adapter.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_ldo_regulator.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_jd9165.h"
#include "driver/ledc.h"
static const char *TAG = "jc1060_display";
static esp_lcd_panel_handle_t s_panel;
static esp_ldo_channel_handle_t s_ldo;
static const jd9165_lcd_init_cmd_t jd9165_init_cmds[] = {
    // {command, {data}, data_size, delay_ms}
    {0x30, (const uint8_t[]){0x00}, 1, 0},
    {0xF7, (const uint8_t[]){0x49, 0x61, 0x02, 0x00}, 4, 0},
    {0x30, (const uint8_t[]){0x01}, 1, 0},
    {0x04, (const uint8_t[]){0x0C}, 1, 0},
    {0x05, (const uint8_t[]){0x00}, 1, 0},     // HBP adjustment
    {0x06, (const uint8_t[]){0x00}, 1, 0},     // VBP adjustment
    {0x0B, (const uint8_t[]){0x11}, 1, 0},     // 2 lanes (0x11), 1 lane would be 0x10
    {0x17, (const uint8_t[]){0x00}, 1, 0},
    {0x20, (const uint8_t[]){0x04}, 1, 0},     // Lane select
    {0x1F, (const uint8_t[]){0x05}, 1, 0},     // HS settle time
    {0x23, (const uint8_t[]){0x00}, 1, 0},     // Close GAS
    {0x25, (const uint8_t[]){0x19}, 1, 0},
    {0x28, (const uint8_t[]){0x18}, 1, 0},
    {0x29, (const uint8_t[]){0x04}, 1, 0},     // VCOM
    {0x2A, (const uint8_t[]){0x01}, 1, 0},     // VCOM
    {0x2B, (const uint8_t[]){0x04}, 1, 0},     // VCOM
    {0x2C, (const uint8_t[]){0x01}, 1, 0},     // VCOM
    {0x30, (const uint8_t[]){0x02}, 1, 0},
    {0x01, (const uint8_t[]){0x22}, 1, 0},
    {0x03, (const uint8_t[]){0x12}, 1, 0},
    {0x04, (const uint8_t[]){0x00}, 1, 0},
    {0x05, (const uint8_t[]){0x64}, 1, 0},
    {0x0A, (const uint8_t[]){0x08}, 1, 0},
    {0x0B, (const uint8_t[]){0x0A, 0x1A, 0x0B, 0x0D, 0x0D, 0x11, 0x10, 0x06, 0x08, 0x1F, 0x1D}, 11, 0},
    {0x0C, (const uint8_t[]){0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D}, 11, 0},
    {0x0D, (const uint8_t[]){0x16, 0x1B, 0x0B, 0x0D, 0x0D, 0x11, 0x10, 0x07, 0x09, 0x1E, 0x1C}, 11, 0},
    {0x0E, (const uint8_t[]){0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D}, 11, 0},
    {0x0F, (const uint8_t[]){0x16, 0x1B, 0x0D, 0x0B, 0x0D, 0x11, 0x10, 0x1C, 0x1E, 0x09, 0x07}, 11, 0},
    {0x10, (const uint8_t[]){0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D}, 11, 0},
    {0x11, (const uint8_t[]){0x0A, 0x1A, 0x0D, 0x0B, 0x0D, 0x11, 0x10, 0x1D, 0x1F, 0x08, 0x06}, 11, 0},
    {0x12, (const uint8_t[]){0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D}, 11, 0},
    {0x14, (const uint8_t[]){0x00, 0x00, 0x11, 0x11}, 4, 0}, // CKV_OFF timing
    {0x18, (const uint8_t[]){0x99}, 1, 0},
    {0x30, (const uint8_t[]){0x06}, 1, 0},
    {0x12, (const uint8_t[]){0x36, 0x2C, 0x2E, 0x3C, 0x38, 0x35, 0x35, 0x32, 0x2E, 0x1D, 0x2B, 0x21, 0x16, 0x29}, 14, 0},
    {0x13, (const uint8_t[]){0x36, 0x2C, 0x2E, 0x3C, 0x38, 0x35, 0x35, 0x32, 0x2E, 0x1D, 0x2B, 0x21, 0x16, 0x29}, 14, 0},
    {0x30, (const uint8_t[]){0x0A}, 1, 0},
    {0x02, (const uint8_t[]){0x4F}, 1, 0},
    {0x0B, (const uint8_t[]){0x40}, 1, 0},
    {0x12, (const uint8_t[]){0x3E}, 1, 0},
    {0x13, (const uint8_t[]){0x78}, 1, 0},
    {0x30, (const uint8_t[]){0x0D}, 1, 0},
    {0x0D, (const uint8_t[]){0x04}, 1, 0},
    {0x10, (const uint8_t[]){0x0C}, 1, 0},
    {0x11, (const uint8_t[]){0x0C}, 1, 0},
    {0x12, (const uint8_t[]){0x0C}, 1, 0},
    {0x13, (const uint8_t[]){0x0C}, 1, 0},
    {0x30, (const uint8_t[]){0x00}, 1, 0},
    {0x11, (const uint8_t[]){0}, 0, 120},     // SLPOUT with 120ms delay
    {0x29, (const uint8_t[]){0}, 0, 20},      // DISPON with 20ms delay
};
esp_lcd_panel_handle_t bsp_display_get_panel_handle(void) { return s_panel; }
void bsp_display_set_backlight(uint8_t pct)
{
    if (pct > 100) pct = 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 1023u * pct / 100u);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}
esp_err_t bsp_display_init(void)
{
    esp_ldo_channel_config_t power = {.chan_id = 3, .voltage_mv = 2500};
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&power, &s_ldo), TAG, "DSI power");
    esp_lcd_dsi_bus_handle_t bus;
    esp_lcd_dsi_bus_config_t config = {
        .bus_id = 0, .num_data_lanes = 2,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT, .lane_bit_rate_mbps = 750};
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&config, &bus), TAG, "DSI bus");
    esp_lcd_panel_io_handle_t io;
    esp_lcd_dbi_io_config_t dbi = {.virtual_channel = 0, .lcd_cmd_bits = 8, .lcd_param_bits = 8};
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(bus, &dbi, &io), TAG, "DBI");
    esp_lcd_dpi_panel_config_t dpi = {
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT, .dpi_clock_freq_mhz = 54,
        .virtual_channel = 0, .in_color_format = LCD_COLOR_FMT_RGB565,
        .num_fbs = BSP_LCD_FRAMEBUFFER_COUNT,
        .video_timing = {.h_size = 1024, .v_size = 600,
            .hsync_pulse_width = 40, .hsync_back_porch = 160, .hsync_front_porch = 160,
            .vsync_pulse_width = 10, .vsync_back_porch = 23, .vsync_front_porch = 12}};
    jd9165_vendor_config_t vendor = {
        .mipi_config = {.dsi_bus = bus, .dpi_config = &dpi},
        .init_cmds = jd9165_init_cmds,
        .init_cmds_size = sizeof(jd9165_init_cmds) / sizeof(jd9165_init_cmds[0])};
    esp_lcd_panel_dev_config_t panel = {.reset_gpio_num = 5,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB, .bits_per_pixel = 24,
        .vendor_config = &vendor};
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_jd9165(io, &panel, &s_panel), TAG, "panel create");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "panel reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "panel init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(s_panel, false), TAG, "INVOFF");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "display on");
    ledc_timer_config_t timer = {.speed_mode = LEDC_LOW_SPEED_MODE, .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_10_BIT, .freq_hz = 5000, .clk_cfg = LEDC_AUTO_CLK};
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "backlight timer");
    ledc_channel_config_t channel = {.gpio_num = 23, .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0, .timer_sel = LEDC_TIMER_0, .duty = 0};
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel), TAG, "backlight");
    bsp_display_set_backlight(80);
    return ESP_OK;
}
