/* SPDX-License-Identifier: MIT
 * JC8012P4A1 display bring-up (10.1", JD9365 over MIPI-DSI, native 800x1280 portrait).
 *
 * SCAFFOLD, NOT COMPILED, NOT RUN. Written before the board arrived (2026-10-13) from
 * the vendor schematic and the Espressif esp_lcd_jd9365 2.0.2 component. Everything marked
 * UNVERIFIED must be checked on the unit before it is trusted. Touch (GSL3680), SD, I2C
 * and audio are in ../../jc-common/board_peripherals.c, gated by
 * CONFIG_PAJONIIIR_BOARD_JC8012P4A1.
 *
 * The panel uses the 204-entry JD9365 register table read from the factory firmware of the V3
 * unit (jd9365_guition_init.inc, see its header for origin and licence) with the component's
 * 800x1280 DPI timing and 1500 Mbps lanes. The Espressif default table gave a black screen
 * and the Guition demo repository's older table gave scrambled vertical bands.
 */
#include "board_adapter.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_ldo_regulator.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_jd9365.h"
#include "driver/gpio.h"

#include "jd9365_guition_init.inc"

static const char *TAG = "jc8012_display";

#define BSP_JC8012_LCD_RST_GPIO   27   /* schematic 2_LCD / 3_ESP32-P4 */
#define BSP_JC8012_LCD_BL_GPIO    23   /* LCD_PWM, LEDC */
#define BSP_JC8012_DSI_LDO_CHAN   3    /* VDD_MIPI_DPHY, 2.5 V as on the other P4 boards */
#define BSP_JC8012_DSI_LANES      2    /* UNVERIFIED: component default, 2 data lanes */
/* Backlight is a plain on/off GPIO for now. On this chip (ESP32-P4 v3.2) every write to the
 * LEDC gamma RAM (0x500D3400..) raised a Store access fault inside ledc_channel_config(), on
 * channel 0 with the automatic clock and on channel 1 with the XTAL clock (the vendor image
 * uses channel 1 without faulting). Cause not found yet, see BOARD_NOTES.md; PWM dimming is
 * a follow-up. */

static esp_lcd_panel_handle_t s_panel;
static esp_ldo_channel_handle_t s_ldo;

esp_lcd_panel_handle_t bsp_display_get_panel_handle(void) { return s_panel; }

void bsp_display_set_backlight(uint8_t pct)
{
    gpio_set_level(BSP_JC8012_LCD_BL_GPIO, pct > 0);
}

esp_err_t bsp_display_init(void)
{
    ESP_LOGI(TAG, "display init: DSI LDO, bus, panel");
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
        .init_cmds = s_guition_init_cmds,   /* panel maker's table, see the .inc header */
        .init_cmds_size = sizeof(s_guition_init_cmds) / sizeof(s_guition_init_cmds[0]),
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

    gpio_config_t bl = {.pin_bit_mask = 1ULL << BSP_JC8012_LCD_BL_GPIO, .mode = GPIO_MODE_OUTPUT};
    ESP_RETURN_ON_ERROR(gpio_config(&bl), TAG, "backlight gpio");
    ESP_LOGI(TAG, "panel on, backlight on");
    bsp_display_set_backlight(100);
    return ESP_OK;
}
