#include "board_adapter.h"
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

static i2c_master_bus_handle_t  s_i2c_bus  = NULL;
static esp_lcd_touch_handle_t   s_touch    = NULL;
#if CONFIG_BSP_ES8311_MONITOR
static i2s_chan_handle_t        s_i2s_tx   = NULL;
#endif
static i2s_chan_handle_t        s_i2s_tx_pcm5102 = NULL;
static bool                     s_i2s_tx_pcm5102_enabled = false;
static esp_codec_dev_handle_t   s_codec    = NULL;
static bsp_audio_out_t          s_audio_out = BSP_AUDIO_OUT_RCA;
static bool                     s_audio_pa_gpio_ready = false;
static bool                     s_speaker_pa_enabled = false;
static bsp_monitor_route_t      s_monitor_route = BSP_MONITOR_ROUTE_HEADPHONES;
static sdmmc_card_t            *s_sd_card  = NULL;
static sd_pwr_ctrl_handle_t     s_sd_pwr   = NULL;

esp_lcd_touch_handle_t bsp_touch_get_handle(void)
{
    return s_touch;
}

i2c_master_bus_handle_t bsp_get_i2c_bus(void)
{
    return s_i2c_bus;
}

// Lazily create the shared I2C master bus (GPIO7/8) used by touch + codec.
static esp_err_t bsp_i2c_bus_init(void)
{
    if (s_i2c_bus) {
        return ESP_OK;
    }
    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .sda_io_num = BSP_I2C_SDA_GPIO,
        .scl_io_num = BSP_I2C_SCL_GPIO,
        .i2c_port   = BSP_I2C_PORT,
    };
    return i2c_new_master_bus(&bus_cfg, &s_i2c_bus);
}

esp_err_t bsp_touch_init(void)
{
    ESP_RETURN_ON_ERROR(bsp_i2c_bus_init(), TAG, "I2C bus init failed");

    // GT911 capacitive touch controller on the shared I2C bus (default addr 0x5D).
    esp_lcd_panel_io_handle_t tp_io = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    tp_io_cfg.scl_speed_hz = 100000;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(s_i2c_bus, &tp_io_cfg, &tp_io),
                        TAG, "GT911 panel IO failed");

    // Native panel is 480x800 portrait; the UI runs 800x480 landscape rotated 90°.
    // swap_xy + mirror_x map raw touch coordinates into the landscape canvas
    // (matches the JC4880 vendor demo's ROTATION_90 configuration).
    esp_lcd_touch_config_t tp_cfg = {
        .x_max        = BSP_LCD_H_RES,   // 480 (native)
        .y_max        = BSP_LCD_V_RES,   // 800 (native)
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = GPIO_NUM_NC,
        .levels = {
            .reset     = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy  = board_capabilities_get()->panel_rotation == 270,
            .mirror_x = board_capabilities_get()->panel_rotation == 270,
            .mirror_y = 0,
        },
    };
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_gt911(tp_io, &tp_cfg, &s_touch),
                        TAG, "GT911 init failed");

    ESP_LOGI(TAG, "GT911 touch ready (I2C SDA=%d SCL=%d, 800x480 mapped)",
             BSP_I2C_SDA_GPIO, BSP_I2C_SCL_GPIO);
    return ESP_OK;
}

esp_codec_dev_handle_t bsp_audio_get_codec_dev(void)
{
    return s_codec;
}

i2s_chan_handle_t bsp_audio_get_main_i2s_tx(void)
{
    return s_i2s_tx_pcm5102;
}

static esp_err_t bsp_audio_pa_gpio_init_once(void)
{
    if (s_audio_pa_gpio_ready) {
        return ESP_OK;
    }

    /* Program the output latch low before switching the pad to output mode, then
     * assert it again afterwards. This prevents a product boot from briefly
     * enabling the retired onboard speaker amplifier. */
    ESP_RETURN_ON_ERROR(gpio_set_level(BSP_AUDIO_PA_GPIO, 0), TAG,
                        "speaker PA safe latch failed");
    gpio_config_t pa_cfg = {
        .mode         = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << BSP_AUDIO_PA_GPIO,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&pa_cfg), TAG, "speaker PA gpio config failed");
    ESP_RETURN_ON_ERROR(gpio_set_level(BSP_AUDIO_PA_GPIO, 0), TAG,
                        "speaker PA safe level failed");
    s_audio_pa_gpio_ready = true;
    s_speaker_pa_enabled = false;
    return ESP_OK;
}

esp_err_t bsp_audio_force_safe_boot_state(void)
{
    ESP_RETURN_ON_ERROR(bsp_audio_pa_gpio_init_once(), TAG,
                        "speaker PA safe boot init failed");
    ESP_RETURN_ON_ERROR(gpio_set_level(BSP_AUDIO_PA_GPIO, 0), TAG,
                        "speaker PA safe boot level failed");
    s_speaker_pa_enabled = false;
    s_audio_out = BSP_AUDIO_OUT_RCA;
    s_monitor_route = BSP_MONITOR_ROUTE_HEADPHONES;
    return ESP_OK;
}

static esp_err_t bsp_audio_init_i2s_pcm5102(void)
{
#if CONFIG_BSP_PCM5102A_MAIN_OUT
    if (s_i2s_tx_pcm5102) {
        return ESP_OK;
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(BSP_PCM5102_I2S_NUM, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_i2s_tx_pcm5102, NULL), TAG, "pcm5102 i2s_new_channel failed");

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(44100),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = BSP_PCM5102_MCLK_GPIO,
            .bclk = BSP_PCM5102_BCLK_GPIO,
            .ws = BSP_PCM5102_WS_GPIO,
            .dout = BSP_PCM5102_DOUT_GPIO,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_i2s_tx_pcm5102, &std_cfg), TAG, "pcm5102 i2s std init failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_i2s_tx_pcm5102), TAG, "pcm5102 i2s enable failed");
    s_i2s_tx_pcm5102_enabled = true;
    ESP_LOGI(TAG, "PCM5102A main out ready: BCLK=%d WS=%d DOUT=%d",
             BSP_PCM5102_BCLK_GPIO, BSP_PCM5102_WS_GPIO, BSP_PCM5102_DOUT_GPIO);
#endif
    return ESP_OK;
}

esp_err_t bsp_audio_init(void)
{
    ESP_RETURN_ON_ERROR(bsp_audio_force_safe_boot_state(), TAG,
                        "speaker PA safe boot state failed");
    ESP_RETURN_ON_ERROR(bsp_i2c_bus_init(), TAG, "I2C bus init failed");

#if !CONFIG_BSP_ES8311_MONITOR
    /* ES8311 monitor disabled: only bring up the PCM5102A MAIN OUT DAC.
       FLX4 cue/headphones use direct USB Audio from P4. */
    ESP_RETURN_ON_ERROR(bsp_audio_init_i2s_pcm5102(), TAG, "PCM5102A init failed");
    ESP_LOGI(TAG, "ES8311 monitor disabled; FLX4 cue uses direct USB Audio");
    return ESP_OK;
#else
    if (s_codec && s_i2s_tx) {
        ESP_RETURN_ON_ERROR(bsp_audio_pa_gpio_init_once(), TAG, "speaker PA gpio init failed");
        ESP_RETURN_ON_ERROR(bsp_audio_set_monitor_route(s_monitor_route), TAG, "monitor route restore failed");
        ESP_RETURN_ON_ERROR(bsp_audio_init_i2s_pcm5102(), TAG, "PCM5102A restore failed");
        return ESP_OK;
    }

    // ── I2S TX channel (master, standard Philips, 16-bit stereo) ─────────────
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(BSP_I2S_NUM, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;   // zero the DMA buffer on underrun (avoids noise)
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_i2s_tx, NULL), TAG, "i2s_new_channel failed");

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(44100),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = BSP_I2S_MCLK_GPIO,
            .bclk = BSP_I2S_BCLK_GPIO,
            .ws   = BSP_I2S_WS_GPIO,
            .dout = BSP_I2S_DOUT_GPIO,
            .din  = BSP_I2S_DIN_GPIO,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_i2s_tx, &std_cfg), TAG, "i2s init std failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_i2s_tx), TAG, "i2s enable failed");

    // ── ES8311 codec via esp_codec_dev (I2C control + I2S data) ──────────────
    audio_codec_i2s_cfg_t i2s_data_cfg = {
        .port       = BSP_I2S_NUM,
        .tx_handle  = s_i2s_tx,
        .rx_handle  = NULL,
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_data_cfg);
    ESP_RETURN_ON_FALSE(data_if, ESP_FAIL, TAG, "i2s data_if alloc failed");

    audio_codec_i2c_cfg_t i2c_ctrl_cfg = {
        .port       = BSP_I2C_PORT,
        .addr       = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = s_i2c_bus,
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_ctrl_cfg);
    ESP_RETURN_ON_FALSE(ctrl_if, ESP_FAIL, TAG, "i2c ctrl_if alloc failed");

    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();

    esp_codec_dev_hw_gain_t gain = {
        .pa_voltage        = 5.0,
        .codec_dac_voltage = 3.3,
    };
    es8311_codec_cfg_t es8311_cfg = {
        .ctrl_if      = ctrl_if,
        .gpio_if      = gpio_if,
        .codec_mode   = ESP_CODEC_DEV_TYPE_OUT,
        /* The BSP owns the PA pin (GPIO11) so it can route output speaker↔RCA and
         * keep the choice across track loads; the codec must not touch it. */
        .pa_pin       = GPIO_NUM_NC,
        .pa_reverted  = false,
        .master_mode  = false,
        .use_mclk     = true,
        .digital_mic  = false,
        .invert_mclk  = false,
        .invert_sclk  = false,
        .hw_gain      = gain,
    };
    const audio_codec_if_t *es8311_if = es8311_codec_new(&es8311_cfg);
    ESP_RETURN_ON_FALSE(es8311_if, ESP_FAIL, TAG, "es8311_codec_new failed");

    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = es8311_if,
        .data_if  = data_if,
    };
    s_codec = esp_codec_dev_new(&dev_cfg);
    ESP_RETURN_ON_FALSE(s_codec, ESP_FAIL, TAG, "esp_codec_dev_new failed");

    esp_codec_dev_set_out_vol(s_codec, 70.0);   // default volume (0..100)

    // BSP-owned power-amp enable (GPIO11): drives the onboard monitor speaker.
    ESP_RETURN_ON_ERROR(bsp_audio_pa_gpio_init_once(), TAG, "speaker PA gpio init failed");
    ESP_RETURN_ON_ERROR(bsp_audio_set_monitor_route(s_monitor_route), TAG, "monitor route init failed");
    ESP_RETURN_ON_ERROR(bsp_audio_init_i2s_pcm5102(), TAG, "PCM5102A init failed");

    ESP_LOGI(TAG, "ES8311 monitor ready (I2S MCLK=%d BCLK=%d WS=%d DOUT=%d, PA=%d, route=%s)",
             BSP_I2S_MCLK_GPIO, BSP_I2S_BCLK_GPIO, BSP_I2S_WS_GPIO, BSP_I2S_DOUT_GPIO, BSP_AUDIO_PA_GPIO,
             s_monitor_route == BSP_MONITOR_ROUTE_SPEAKER ? "built-in speaker" : "headphones");
    return ESP_OK;
#endif /* CONFIG_BSP_ES8311_MONITOR */
}

esp_err_t bsp_audio_set_speaker_pa_enabled(bool enabled)
{
    ESP_RETURN_ON_ERROR(bsp_audio_pa_gpio_init_once(), TAG, "speaker PA gpio init failed");
#if BSP_SPEAKER_ROUTE_RETIRED
    if (enabled) {
        ESP_LOGE(TAG, "speaker PA route is unavailable in the PCM5102A product configuration");
        (void)gpio_set_level(BSP_AUDIO_PA_GPIO, 0);
        s_speaker_pa_enabled = false;
        return ESP_ERR_NOT_SUPPORTED;
    }
#endif
    ESP_RETURN_ON_ERROR(gpio_set_level(BSP_AUDIO_PA_GPIO, enabled ? 1 : 0), TAG,
                        "speaker PA level failed");
    s_speaker_pa_enabled = enabled;
    ESP_LOGI(TAG, "monitor speaker PA %s", enabled ? "on" : "off");
    return ESP_OK;
}

bool bsp_audio_get_speaker_pa_enabled(void)
{
    return s_speaker_pa_enabled;
}

esp_err_t bsp_audio_set_monitor_route(bsp_monitor_route_t route)
{
    switch (route) {
    case BSP_MONITOR_ROUTE_HEADPHONES:
        ESP_RETURN_ON_ERROR(bsp_audio_set_speaker_pa_enabled(false), TAG, "speaker PA off failed");
        s_monitor_route = route;
        ESP_LOGI(TAG, "monitor route → headphones");
        return ESP_OK;
    case BSP_MONITOR_ROUTE_SPEAKER:
#if BSP_SPEAKER_ROUTE_RETIRED
        (void)bsp_audio_set_speaker_pa_enabled(false);
        return ESP_ERR_NOT_SUPPORTED;
#else
        ESP_RETURN_ON_ERROR(bsp_audio_set_speaker_pa_enabled(true), TAG, "speaker PA on failed");
        s_monitor_route = route;
        ESP_LOGI(TAG, "monitor route → built-in speaker");
        return ESP_OK;
#endif
    default:
        return ESP_ERR_INVALID_ARG;
    }
}

bsp_monitor_route_t bsp_audio_get_monitor_route(void)
{
    return s_monitor_route;
}

esp_err_t bsp_audio_set_output(bsp_audio_out_t out)
{
    if (out != BSP_AUDIO_OUT_SPEAKER && out != BSP_AUDIO_OUT_RCA) {
        return ESP_ERR_INVALID_ARG;
    }
#if BSP_SPEAKER_ROUTE_RETIRED
    if (out == BSP_AUDIO_OUT_SPEAKER) {
        (void)bsp_audio_set_speaker_pa_enabled(false);
        return ESP_ERR_NOT_SUPPORTED;
    }
#endif
    esp_err_t rc = bsp_audio_set_monitor_route(out == BSP_AUDIO_OUT_SPEAKER
                                                ? BSP_MONITOR_ROUTE_SPEAKER
                                                : BSP_MONITOR_ROUTE_HEADPHONES);
    if (rc == ESP_OK) {
        s_audio_out = out;
    }
    return rc;
}

bsp_audio_out_t bsp_audio_get_output(void)
{
    return s_audio_out;
}

esp_err_t bsp_audio_main_i2s_set_sample_rate(uint32_t sample_rate)
{
#if CONFIG_BSP_PCM5102A_MAIN_OUT
    if (!s_i2s_tx_pcm5102 || sample_rate == 0u) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_i2s_tx_pcm5102_enabled) {
        ESP_RETURN_ON_ERROR(i2s_channel_disable(s_i2s_tx_pcm5102), TAG,
                            "pcm5102 disable failed");
        s_i2s_tx_pcm5102_enabled = false;
    }
    i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate);
    ESP_RETURN_ON_ERROR(i2s_channel_reconfig_std_clock(s_i2s_tx_pcm5102, &clk_cfg), TAG, "pcm5102 clock reconfig failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_i2s_tx_pcm5102), TAG, "pcm5102 enable failed");
    s_i2s_tx_pcm5102_enabled = true;
    return ESP_OK;
#else
    (void)sample_rate;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t bsp_audio_main_i2s_abort_write(void)
{
#if CONFIG_BSP_PCM5102A_MAIN_OUT
    if (!s_i2s_tx_pcm5102) return ESP_ERR_INVALID_STATE;
    if (!s_i2s_tx_pcm5102_enabled) return ESP_OK;
    esp_err_t rc = i2s_channel_disable(s_i2s_tx_pcm5102);
    if (rc == ESP_OK) s_i2s_tx_pcm5102_enabled = false;
    return rc;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t bsp_sd_init(void)
{
    if (s_sd_card) {
        return ESP_OK;
    }

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0;          // JC4880 SD pins GPIO39-44 are wired to P4 SDMMC slot 0
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;   // conservative 20 MHz bring-up speed
#if defined(CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE) && \
    ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    host.init = bsp_sdmmc_host_already_initialized;
#endif
    if (!s_sd_pwr) {
        sd_pwr_ctrl_ldo_config_t ldo_config = {
            .ldo_chan_id = BSP_SD_LDO_CHAN,
        };
        esp_err_t pwr_rc = sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &s_sd_pwr);
        if (pwr_rc != ESP_OK) {
            ESP_LOGW(TAG, "SD LDO%d power control unavailable (%s) — /sd unavailable",
                     BSP_SD_LDO_CHAN, esp_err_to_name(pwr_rc));
            return ESP_OK;
        }
    }
    host.pwr_ctrl_handle = s_sd_pwr;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.clk = GPIO_NUM_43;
    slot_config.cmd = GPIO_NUM_44;
    slot_config.d0  = GPIO_NUM_39;
    slot_config.d1  = GPIO_NUM_40;
    slot_config.d2  = GPIO_NUM_41;
    slot_config.d3  = GPIO_NUM_42;
    slot_config.width = 4;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 16 * 1024,
    };

    ESP_LOGI(TAG, "mounting SD at /sd (slot=%d width=%d clk=%d cmd=%d d0=%d d1=%d d2=%d d3=%d)",
             host.slot, slot_config.width, slot_config.clk, slot_config.cmd,
             slot_config.d0, slot_config.d1, slot_config.d2, slot_config.d3);

    esp_err_t rc = ESP_FAIL;
    for (int attempt = 1; attempt <= BSP_SD_MOUNT_ATTEMPTS; attempt++) {
        rc = esp_vfs_fat_sdmmc_mount("/sd", &host, &slot_config, &mount_config, &s_sd_card);
        if (rc == ESP_OK) {
            break;
        }
        // esp_vfs_fat_sdmmc_mount tears down the slot on failure, so a fresh
        // call re-runs the full SD init sequence on the next attempt.
        s_sd_card = NULL;
        ESP_LOGW(TAG, "SD mount attempt %d/%d failed (%s)%s",
                 attempt, BSP_SD_MOUNT_ATTEMPTS, esp_err_to_name(rc),
                 attempt < BSP_SD_MOUNT_ATTEMPTS ? " — retrying" : "");
        if (attempt < BSP_SD_MOUNT_ATTEMPTS) {
            vTaskDelay(pdMS_TO_TICKS(BSP_SD_MOUNT_RETRY_DELAY_MS));
        }
    }
    if (rc != ESP_OK) {
        s_sd_card = NULL;
        ESP_LOGW(TAG, "SD mount skipped (%s) — /sd unavailable; USB media path continues",
                 esp_err_to_name(rc));
        return ESP_OK;
    }

    ESP_LOGI(TAG, "SD card mounted at /sd");
    sdmmc_card_print_info(stdout, s_sd_card);
    return ESP_OK;
}

bool bsp_sd_is_mounted(void)
{
    return s_sd_card != NULL;
}

esp_err_t bsp_sd_get_status(bsp_sd_status_t *out_status)
{
    ESP_RETURN_ON_FALSE(out_status, ESP_ERR_INVALID_ARG, TAG, "missing SD status output");
    memset(out_status, 0, sizeof(*out_status));

    if (!s_sd_card) {
        return ESP_ERR_NOT_FOUND;
    }

    FATFS *fs = NULL;
    DWORD free_clusters = 0;
    if (f_getfree("/sd", &free_clusters, &fs) != FR_OK || !fs) {
        return ESP_ERR_NOT_FOUND;
    }

    uint32_t sector_size = 512u;
#if FF_MAX_SS != FF_MIN_SS
    sector_size = fs->ssize;
#endif
    uint64_t cluster_bytes = (uint64_t)fs->csize * (uint64_t)sector_size;
    uint64_t total_clusters = fs->n_fatent > 2 ? (uint64_t)(fs->n_fatent - 2) : 0;

    out_status->mounted = true;
    out_status->sector_size = sector_size;
    out_status->free_bytes = (uint64_t)free_clusters * cluster_bytes;
    out_status->total_bytes = total_clusters * cluster_bytes;
    return ESP_OK;
}
