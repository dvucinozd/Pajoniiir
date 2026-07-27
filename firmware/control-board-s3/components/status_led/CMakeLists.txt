idf_component_register(
    SRCS "status_led.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_driver_gpio log esp_timer
)
