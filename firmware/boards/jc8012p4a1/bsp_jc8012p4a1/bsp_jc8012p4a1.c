/* SPDX-License-Identifier: MIT
 * JC8012P4A1 display bring-up (10.1", JD9365 over MIPI-DSI, native 800x1280 portrait).
 *
 * SCAFFOLD, NOT COMPILED, NOT RUN. Written before the board arrived (2026-10-13) from
 * the vendor schematic and the Espressif esp_lcd_jd9365 2.0.2 component. Everything marked
 * UNVERIFIED must be checked on the unit before it is trusted. Touch (GSL3680), SD, I2C
 * and audio are in ../../jc-common/board_peripherals.c, gated by
 * CONFIG_PAJONIIIR_BOARD_JC8012P4A1.
 *
 * The panel uses the component's DEFAULT JD9365 register table and its 800x1280 60 Hz
 * DPI timing (Apache-2.0 component). The "V2"/"V3" hardware revisions is described elsewhere as
 * a "new panel" with its own init sequence; if the picture is wrong, a revision-specific table
 * passed through jd9365_vendor_config_t.init_cmds is the next step, from a source whose
 * licence allows it (the EspControl repository is PolyForm Noncommercial, do not copy it).
 */
#include "board_adapter.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_ldo_regulator.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_jd9365.h"
#include "driver/ledc.h"

static const char *TAG = "jc8012_display";

#define BSP_JC8012_LCD_RST_GPIO   27   /* schematic 2_LCD / 3_ESP32-P4 */
#define BSP_JC8012_LCD_BL_GPIO    23   /* LCD_PWM, LEDC */
#define BSP_JC8012_DSI_LDO_CHAN   3    /* VDD_MIPI_DPHY, 2.5 V as on the other P4 boards */
#define BSP_JC8012_DSI_LANES      2    /* UNVERIFIED: component default, 2 data lanes */

static esp_lcd_panel_handle_t s_panel;
static esp_ldo_channel_handle_t s_ldo;

esp_lcd_panel_handle_t bsp_display_get_panel_handle(void) { return s_panel; }

void bsp_display_set_backlight(uint8_t pct)
{
    if (pct > 100) pct = 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 1023u * pct / 100u);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

esp_err_t bsp_display_init(void)
{
    esp_ldo_channel_config_t power = {.chan_id = BSP_JC8012_DSI_LDO_CHAN, .voltage_mv = 2500};
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&power, &s_ldo), TAG, "DSI power");

    esp_lcd_dsi_bus_handle_t bus;
    esp_lcd_dsi_bus_config_t bus_cfg = JD9365_PANEL_BUS_DSI_2CH_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&bus_cfg, &bus), TAG, "DSI bus");

    esp_lcd_panel_io_handle_t io;
    esp_lcd_dbi_io_config_t dbi = JD9365_PANEL_IO_DBI_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(bus, &dbi, &io), TAG, "DBI");

    /* 800x1280, 80 MHz pixel clock, porches from the component (about 60 Hz). RGB565 like
     * the other P4 boards; one framebuffer until a refresh-boundary swap is validated. */
    esp_lcd_dpi_panel_config_t dpi = JD9365_800_1280_PANEL_60HZ_DPI_CONFIG_CF(LCD_COLOR_FMT_RGB565);
    dpi.num_fbs = BSP_LCD_FRAMEBUFFER_COUNT;

    jd9365_vendor_config_t vendor = {
        .init_cmds = NULL,       /* component default register table */
        .init_cmds_size = 0,
        .mipi_config = {.dsi_bus = bus, .dpi_config = &dpi, .lane_num = BSP_JC8012_DSI_LANES},
    };
    esp_lcd_panel_dev_config_t panel = {
        .reset_gpio_num = BSP_JC8012_LCD_RST_GPIO,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_jd9365(io, &panel, &s_panel), TAG, "panel create");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "panel reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "panel init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "display on");

    ledc_timer_config_t timer = {.speed_mode = LEDC_LOW_SPEED_MODE, .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_10_BIT, .freq_hz = 5000, .clk_cfg = LEDC_AUTO_CLK};
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "backlight timer");
    ledc_channel_config_t channel = {.gpio_num = BSP_JC8012_LCD_BL_GPIO, .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0, .timer_sel = LEDC_TIMER_0, .duty = 0};
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel), TAG, "backlight");
    bsp_display_set_backlight(80);
    return ESP_OK;
}
