idf_component_register(
    SRCS "status_led.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES driver log esp_timer
)
