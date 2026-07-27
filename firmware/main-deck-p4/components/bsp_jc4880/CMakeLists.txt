idf_component_register(
    SRCS "bsp_jc4880.c"
    INCLUDE_DIRS "include"
    REQUIRES log esp_driver_gpio esp_driver_ledc esp_driver_i2c esp_driver_i2s esp_driver_sdmmc esp_lcd esp_lcd_st7701 esp_lcd_touch esp_lcd_touch_gt911 esp_codec_dev fatfs sdmmc
)
