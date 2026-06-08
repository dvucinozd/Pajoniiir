#pragma once

// Board support for JC4880P443C_I_W (ESP32-P4 + ST7701S + GT911 + ES8311)
//
// Pin reference (from board analysis):
//   LCD backlight  GPIO23      LCD reset     GPIO5
//   Touch I2C SDA  GPIO7       Touch I2C SCL GPIO8    addr 0x5D
//   SDMMC D0-D3    GPIO39-42   CMD GPIO44    CLK GPIO43
//   I2S MCLK       GPIO13      BCLK GPIO12   LRCK GPIO10
//   I2S DIN        GPIO48      DOUT GPIO9    PA   GPIO11
//   Codec I2C      GPIO7/8     addr 0x18

#include "esp_err.h"
#include "esp_lcd_types.h"
#include "esp_lcd_touch.h"
#include "driver/i2c_master.h"
#include "esp_codec_dev.h"
#include <stdbool.h>
#include <stdint.h>

// Native panel geometry (ST7701S is wired in 480x800 portrait).
// The UI runs in 800x480 landscape via a 90° LVGL software rotation.
#define BSP_LCD_H_RES   480
#define BSP_LCD_V_RES   800

esp_err_t bsp_display_init(void);   // ST7701S MIPI DSI, 480x800, landscape rotation

// Set LCD backlight brightness 0..100 % (LEDC PWM on GPIO23).
void      bsp_display_set_backlight(uint8_t pct);
esp_err_t bsp_touch_init(void);     // GT911 on I2C GPIO7/8
esp_err_t bsp_audio_init(void);     // ES8311 codec + I2S
esp_err_t bsp_sd_init(void);        // SDMMC on GPIO39-44 (internal storage/config)

typedef struct {
    bool mounted;
    uint64_t total_bytes;
    uint64_t free_bytes;
    uint32_t sector_size;
} bsp_sd_status_t;

bool bsp_sd_is_mounted(void);
esp_err_t bsp_sd_get_status(bsp_sd_status_t *out_status);

// Returns the MIPI-DSI panel handle created by bsp_display_init(), or NULL if
// the display has not been initialised yet. The UI layer uses this to wire up
// the LVGL flush callback and the DPI frame-transfer-done event.
esp_lcd_panel_handle_t bsp_display_get_panel_handle(void);

// Returns the GT911 touch handle created by bsp_touch_init(), or NULL. The UI
// layer uses this to register the LVGL pointer input device.
esp_lcd_touch_handle_t bsp_touch_get_handle(void);

// Returns the shared I2C master bus (GPIO7/8), created on first touch/audio init.
i2c_master_bus_handle_t bsp_get_i2c_bus(void);

// Returns the ES8311 codec device created by bsp_audio_init(), or NULL. The
// audio engine opens it (esp_codec_dev_open) at the track sample rate and writes
// PCM frames via esp_codec_dev_write().
esp_codec_dev_handle_t bsp_audio_get_codec_dev(void);

// Audio output routing. The ES8311 DAC line output (DAC_OUT_P/N) feeds both the
// onboard speaker power-amp (enabled by GPIO11) and the RCA line-out tap.
//   SPEAKER — power amp ON  (audible on the onboard speaker)
//   RCA     — power amp OFF (clean line-level only, for the RCA jacks / mixer)
typedef enum {
    BSP_AUDIO_OUT_SPEAKER = 0,
    BSP_AUDIO_OUT_RCA,
} bsp_audio_out_t;

void            bsp_audio_set_output(bsp_audio_out_t out);
bsp_audio_out_t bsp_audio_get_output(void);
