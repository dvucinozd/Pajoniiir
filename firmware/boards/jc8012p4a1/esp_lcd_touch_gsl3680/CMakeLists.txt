idf_component_register(SRCS "esp_lcd_touch_gsl3680.c" "gsl_point_id.c" INCLUDE_DIRS "include"
    REQUIRES esp_lcd espressif__esp_lcd_touch esp_driver_gpio esp_driver_i2c)
