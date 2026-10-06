#include "bsp_jc4880.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_idf_version.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_ldo_regulator.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_st7701.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "driver/sdmmc_host.h"
#include "ff.h"

#include <string.h>

static const char *TAG = "bsp";

// ── Display pins ─────────────────────────────────────────────────────────────
#define BSP_LCD_RST_GPIO        GPIO_NUM_5
#define BSP_LCD_BL_GPIO         GPIO_NUM_23
#define BSP_LCD_BL_ON_LEVEL     1
// Backlight PWM (LEDC): 10-bit @ 5 kHz on GPIO23 for dimmable brightness.
#define BSP_BL_LEDC_TIMER       LEDC_TIMER_0
#define BSP_BL_LEDC_CHANNEL     LEDC_CHANNEL_0
#define BSP_BL_LEDC_MODE        LEDC_LOW_SPEED_MODE
#define BSP_BL_LEDC_RES         LEDC_TIMER_10_BIT
#define BSP_BL_LEDC_FREQ_HZ     5000
#define BSP_BL_DEFAULT_PCT      80

// ── Shared I2C bus (touch GT911 @0x5D + audio codec ES8311 @0x18) ────────────
#define BSP_I2C_PORT            I2C_NUM_1
#define BSP_I2C_SDA_GPIO        GPIO_NUM_7
#define BSP_I2C_SCL_GPIO        GPIO_NUM_8

// ── Audio: ES8311 codec + I2S (pins from JC4880 vendor BSP) ──────────────────
#define BSP_I2S_NUM             I2S_NUM_0
#define BSP_I2S_MCLK_GPIO       GPIO_NUM_13
#define BSP_I2S_BCLK_GPIO       GPIO_NUM_12
#define BSP_I2S_WS_GPIO         GPIO_NUM_10
#define BSP_I2S_DOUT_GPIO       GPIO_NUM_9    // ESP → codec DAC
#define BSP_I2S_DIN_GPIO        GPIO_NUM_48   // codec ADC → ESP (mic, unused for playback)
#define BSP_AUDIO_PA_GPIO       GPIO_NUM_11   // power-amp enable

#if CONFIG_BSP_PCM5102A_MAIN_OUT && !CONFIG_BSP_ES8311_MONITOR
#define BSP_SPEAKER_ROUTE_RETIRED 1
#else
#define BSP_SPEAKER_ROUTE_RETIRED 0
#endif

// ── Main Out: PCM5102A on JP1 candidate pins ────────────────────────────────
#define BSP_PCM5102_I2S_NUM        I2S_NUM_1
#define BSP_PCM5102_BCLK_GPIO      GPIO_NUM_50
#define BSP_PCM5102_WS_GPIO        GPIO_NUM_52
#define BSP_PCM5102_DOUT_GPIO      GPIO_NUM_51
#define BSP_PCM5102_MCLK_GPIO      I2S_GPIO_UNUSED

// Vendor P4 function board BSP powers the uSD slot through on-chip LDO channel 4.
#define BSP_SD_LDO_CHAN         4
// On a cold reset the card's power/state may not have settled before the first
// ACMD41, which surfaces as a send_op_cond timeout. Retry the mount a few times
// with a short settle delay so /sd comes up reliably at boot.
#define BSP_SD_MOUNT_ATTEMPTS       3
#define BSP_SD_MOUNT_RETRY_DELAY_MS 150

#if defined(CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE) && \
    ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
/* ESP-Hosted's IDF 6 constructor owns the one physical SDMMC controller and
 * registers SDIO slot 1 before app_main(). The microSD card shares that
 * controller on slot 0, so a second sdmmc_host_init() must be skipped. Keep the
 * default slot-aware deinit_p callback: it removes only slot 0 after a failed
 * mount or unmount while the Hosted slot keeps the controller alive. */
static esp_err_t bsp_sdmmc_host_already_initialized(void)
{
    return ESP_OK;
}
#endif

// ── MIPI DSI PHY power (ESP32-P4 internal LDO VO3 → VDD_MIPI_DPHY 2.5 V) ──────
#define BSP_MIPI_LDO_CHAN       3
#define BSP_MIPI_LDO_MV         2500

// ── MIPI DSI link ────────────────────────────────────────────────────────────
#define BSP_DSI_LANE_NUM        2
#define BSP_DSI_LANE_MBPS       500     // bit rate per data lane

// ── ST7701S video timing (480x800 portrait) ──────────────────────────────────
//    Horizontal and sync/back-porch values come from the JC4880P443C_I_W
//    vendor demo. VFP is extended to phase-lock UI work to an approximately
//    50 Hz panel refresh while retaining the panel's 34 MHz DPI pixel clock:
//      Htotal = 480 + 12 + 42 + 42 = 576
//      VFP = round(34 MHz / (576 * 50 Hz)) - (800 + 2 + 8) = 371
//      actual refresh = 34 MHz / (576 * 1181) = 49.981 Hz
#define BSP_DPI_CLK_MHZ         34
#define BSP_LCD_HSYNC           12
#define BSP_LCD_HBP             42
#define BSP_LCD_HFP             42
#define BSP_LCD_VSYNC           2
#define BSP_LCD_VBP             8
#define BSP_LCD_VFP             371

static esp_lcd_panel_handle_t s_panel;
static esp_ldo_channel_handle_t s_mipi_ldo;
// ST7701 power/gamma initialisation sequence for the JC4880P443C_I_W panel.
// Copied verbatim from the vendor demo — these registers are panel-specific
// (the esp_lcd_st7701 component's built-in defaults do NOT match this glass,
// which is why the screen stayed blank with init_cmds = NULL).
static const st7701_lcd_init_cmd_t s_st7701_init_cmds[] = {
    {0xFF, (uint8_t []){0x77, 0x01, 0x00, 0x00, 0x13}, 5, 0},
    {0xEF, (uint8_t []){0x08}, 1, 0},
    {0xFF, (uint8_t []){0x77, 0x01, 0x00, 0x00, 0x10}, 5, 0},
    {0xC0, (uint8_t []){0x63, 0x00}, 2, 0},
    {0xC1, (uint8_t []){0x0D, 0x02}, 2, 0},
    {0xC2, (uint8_t []){0x10, 0x08}, 2, 0},
    {0xCC, (uint8_t []){0x10}, 1, 0},
    {0xB0, (uint8_t []){0x80, 0x09, 0x53, 0x0C, 0xD0, 0x07, 0x0C, 0x09, 0x09, 0x28, 0x06, 0xD4, 0x13, 0x69, 0x2B, 0x71}, 16, 0},
    {0xB1, (uint8_t []){0x80, 0x94, 0x5A, 0x10, 0xD3, 0x06, 0x0A, 0x08, 0x08, 0x25, 0x03, 0xD3, 0x12, 0x66, 0x6A, 0x0D}, 16, 0},
    {0xFF, (uint8_t []){0x77, 0x01, 0x00, 0x00, 0x11}, 5, 0},
    {0xB0, (uint8_t []){0x5D}, 1, 0},
    {0xB1, (uint8_t []){0x58}, 1, 0},
    {0xB2, (uint8_t []){0x87}, 1, 0},
    {0xB3, (uint8_t []){0x80}, 1, 0},
    {0xB5, (uint8_t []){0x4E}, 1, 0},
    {0xB7, (uint8_t []){0x85}, 1, 0},
    {0xB8, (uint8_t []){0x21}, 1, 0},
    {0xB9, (uint8_t []){0x10, 0x1F}, 2, 0},
    {0xBB, (uint8_t []){0x03}, 1, 0},
    {0xBC, (uint8_t []){0x00}, 1, 0},
    {0xC1, (uint8_t []){0x78}, 1, 0},
    {0xC2, (uint8_t []){0x78}, 1, 0},
    {0xD0, (uint8_t []){0x88}, 1, 0},
    {0xE0, (uint8_t []){0x00, 0x3A, 0x02}, 3, 0},
    {0xE1, (uint8_t []){0x04, 0xA0, 0x00, 0xA0, 0x05, 0xA0, 0x00, 0xA0, 0x00, 0x40, 0x40}, 11, 0},
    {0xE2, (uint8_t []){0x30, 0x00, 0x40, 0x40, 0x32, 0xA0, 0x00, 0xA0, 0x00, 0xA0, 0x00, 0xA0, 0x00}, 13, 0},
    {0xE3, (uint8_t []){0x00, 0x00, 0x33, 0x33}, 4, 0},
    {0xE4, (uint8_t []){0x44, 0x44}, 2, 0},
    {0xE5, (uint8_t []){0x09, 0x2E, 0xA0, 0xA0, 0x0B, 0x30, 0xA0, 0xA0, 0x05, 0x2A, 0xA0, 0xA0, 0x07, 0x2C, 0xA0, 0xA0}, 16, 0},
    {0xE6, (uint8_t []){0x00, 0x00, 0x33, 0x33}, 4, 0},
    {0xE7, (uint8_t []){0x44, 0x44}, 2, 0},
    {0xE8, (uint8_t []){0x08, 0x2D, 0xA0, 0xA0, 0x0A, 0x2F, 0xA0, 0xA0, 0x04, 0x29, 0xA0, 0xA0, 0x06, 0x2B, 0xA0, 0xA0}, 16, 0},
    {0xEB, (uint8_t []){0x00, 0x00, 0x4E, 0x4E, 0x00, 0x00, 0x00}, 7, 0},
    {0xEC, (uint8_t []){0x08, 0x01}, 2, 0},
    {0xED, (uint8_t []){0xB0, 0x2B, 0x98, 0xA4, 0x56, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xF7, 0x65, 0x4A, 0x89, 0xB2, 0x0B}, 16, 0},
    {0xEF, (uint8_t []){0x08, 0x08, 0x08, 0x45, 0x3F, 0x54}, 6, 0},
    {0xFF, (uint8_t []){0x77, 0x01, 0x00, 0x00, 0x00}, 5, 0},
    {0x11, (uint8_t []){0x00}, 1, 120},   // sleep out, 120 ms delay
    {0x29, (uint8_t []){0x00}, 1, 20},    // display on, 20 ms delay
};

esp_lcd_panel_handle_t bsp_display_get_panel_handle(void)
{
    return s_panel;
}

/* BSP and UI backend must agree on the framebuffer count; the backend has no
 * inactive-buffer swap, so anything other than one would silently reserve PSRAM
 * that is never scanned. */
_Static_assert(BSP_LCD_FRAMEBUFFER_COUNT == 1u,
               "the current LVGL/PPA backend supports exactly one DPI framebuffer");

esp_err_t bsp_display_init(void)
{
    // ── 1. Power up the MIPI DSI PHY via the internal LDO ────────────────────
    esp_ldo_channel_config_t ldo_cfg = {
        .chan_id    = BSP_MIPI_LDO_CHAN,
        .voltage_mv = BSP_MIPI_LDO_MV,
    };
    ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo_cfg, &s_mipi_ldo));
    ESP_LOGI(TAG, "MIPI DSI PHY powered (LDO VO%d @ %d mV)", BSP_MIPI_LDO_CHAN, BSP_MIPI_LDO_MV);

    // ── 2. Create the DSI bus (also initialises the DSI PHY) ─────────────────
    esp_lcd_dsi_bus_handle_t dsi_bus = NULL;
    esp_lcd_dsi_bus_config_t bus_cfg = {
        .bus_id             = 0,
        .num_data_lanes     = BSP_DSI_LANE_NUM,
        .phy_clk_src        = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = BSP_DSI_LANE_MBPS,
    };
    ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bus_cfg, &dsi_bus));

    // ── 3. DBI command interface (sends panel init commands over DSI) ────────
    esp_lcd_panel_io_handle_t dbi_io = NULL;
    esp_lcd_dbi_io_config_t dbi_cfg = {
        .virtual_channel = 0,
        .lcd_cmd_bits    = 8,
        .lcd_param_bits  = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(dsi_bus, &dbi_cfg, &dbi_io));

    // ── 4. DPI video stream config (RGB565, vendor timing) ───────────────────
    esp_lcd_dpi_panel_config_t dpi_cfg = {
        .virtual_channel    = 0,
        .dpi_clk_src        = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = BSP_DPI_CLK_MHZ,
        .pixel_format       = LCD_COLOR_PIXEL_FORMAT_RGB565,
        // One framebuffer, matching what the backend actually does. This asked
        // for three, which reserved roughly 1.54 MiB of PSRAM for two buffers
        // that were never scanned: the partial LVGL/PPA backend rotates into
        // framebuffer zero and never issues a draw or swap for the other two.
        // Do not raise this again until a real refresh-boundary inactive-buffer
        // swap exists and has been validated on hardware.
        .num_fbs            = BSP_LCD_FRAMEBUFFER_COUNT,
        .video_timing = {
            .h_size            = BSP_LCD_H_RES,
            .v_size            = BSP_LCD_V_RES,
            .hsync_pulse_width = BSP_LCD_HSYNC,
            .hsync_back_porch  = BSP_LCD_HBP,
            .hsync_front_porch = BSP_LCD_HFP,
            .vsync_pulse_width = BSP_LCD_VSYNC,
            .vsync_back_porch  = BSP_LCD_VBP,
            .vsync_front_porch = BSP_LCD_VFP,
        },
        // NOTE: keep DMA2D OFF. The UI rotates into one of the DPI driver's
        // own framebuffers with PPA, then calls draw_bitmap() with that buffer.
        // The driver sees no copy is needed and only switches the framebuffer.
        .flags.use_dma2d = false,
    };

    // ── 5. ST7701S vendor panel (panel-specific init sequence) ────────────────
    st7701_vendor_config_t vendor_cfg = {
        .init_cmds      = s_st7701_init_cmds,
        .init_cmds_size = sizeof(s_st7701_init_cmds) / sizeof(s_st7701_init_cmds[0]),
        .mipi_config = {
            .dsi_bus    = dsi_bus,
            .dpi_config = &dpi_cfg,
        },
        .flags = {
            .use_mipi_interface = 1,   // select MIPI-DSI path (default is RGB)
        },
    };
    esp_lcd_panel_dev_config_t dev_cfg = {
        .reset_gpio_num = BSP_LCD_RST_GPIO,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,          // RGB565
        .vendor_config  = &vendor_cfg,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7701(dbi_io, &dev_cfg, &s_panel));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_LOGI(TAG, "ST7701S panel up (%dx%d, %d MHz DPI, RGB565, single framebuffer)",
             BSP_LCD_H_RES, BSP_LCD_V_RES, BSP_DPI_CLK_MHZ);

    // ── 6. Backlight: LEDC PWM on GPIO23 (dimmable) ──────────────────────────
    ledc_timer_config_t bl_timer = {
        .speed_mode      = BSP_BL_LEDC_MODE,
        .timer_num       = BSP_BL_LEDC_TIMER,
        .duty_resolution = BSP_BL_LEDC_RES,
        .freq_hz         = BSP_BL_LEDC_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&bl_timer));
    ledc_channel_config_t bl_chan = {
        .gpio_num   = BSP_LCD_BL_GPIO,
        .speed_mode = BSP_BL_LEDC_MODE,
        .channel    = BSP_BL_LEDC_CHANNEL,
        .timer_sel  = BSP_BL_LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&bl_chan));
    bsp_display_set_backlight(BSP_BL_DEFAULT_PCT);   // app_main overrides with the saved value
    ESP_LOGI(TAG, "backlight PWM ready (GPIO%d, %d%%)", BSP_LCD_BL_GPIO, BSP_BL_DEFAULT_PCT);

    return ESP_OK;
}

void bsp_display_set_backlight(uint8_t pct)
{
    if (pct > 100) pct = 100;
    // 10-bit duty: 0..1023
    uint32_t duty = (1023u * pct) / 100u;
    ledc_set_duty(BSP_BL_LEDC_MODE, BSP_BL_LEDC_CHANNEL, duty);
    ledc_update_duty(BSP_BL_LEDC_MODE, BSP_BL_LEDC_CHANNEL);
}
