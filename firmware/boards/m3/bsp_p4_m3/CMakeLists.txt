idf_component_register(
    SRCS "bsp_p4_m3.c"
    INCLUDE_DIRS "include"
    REQUIRES board_adapter esp_hosted log esp_driver_gpio esp_driver_i2c esp_driver_i2s esp_driver_sdmmc esp_lcd esp_lcd_touch esp_lcd_touch_ft5x06 esp_codec_dev fatfs sdmmc
    PRIV_INCLUDE_DIRS "../../../p4-core/components/audio_engine/include"
    PRIV_REQUIRES esp_hal_lcd esp_timer esp_mm
)

# DSI-506 uses the hardware-accepted burst/no-frame-ACK stream. Bounded ID
# reads still use the pinned IDF private bus; no unverified vendor init is sent.
target_include_directories(${COMPONENT_LIB} PRIVATE
    "${IDF_PATH}/components/esp_lcd/dsi"
)
