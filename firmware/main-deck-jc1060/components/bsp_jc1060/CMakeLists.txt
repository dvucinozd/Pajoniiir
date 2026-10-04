idf_component_register(SRCS "bsp_jc1060.c" INCLUDE_DIRS "."
    REQUIRES board_adapter esp_lcd esp_lcd_jd9165 esp_driver_ledc esp_hw_support)
