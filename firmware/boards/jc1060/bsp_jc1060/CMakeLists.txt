idf_component_register(SRCS "bsp_jc1060.c" "../../jc-common/board_peripherals.c" INCLUDE_DIRS "."
    PRIV_INCLUDE_DIRS "../../../p4-core/components/audio_engine/include"
    PRIV_REQUIRES esp_timer
    REQUIRES board_adapter esp_lcd_touch_gt911 esp_codec_dev esp_driver_i2c esp_driver_i2s esp_driver_sdmmc fatfs sdmmc esp_lcd esp_lcd_jd9165 esp_driver_ledc esp_hw_support)
