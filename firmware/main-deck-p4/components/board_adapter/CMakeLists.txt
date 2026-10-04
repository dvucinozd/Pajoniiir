idf_component_register(SRCS "board_capabilities.c" "board_peripherals.c" INCLUDE_DIRS "include"
    REQUIRES esp_lcd esp_lcd_touch esp_lcd_touch_gt911 esp_codec_dev esp_driver_i2c
             esp_driver_i2s esp_driver_gpio esp_driver_ledc esp_driver_sdmmc
             fatfs sdmmc esp_hw_support)
