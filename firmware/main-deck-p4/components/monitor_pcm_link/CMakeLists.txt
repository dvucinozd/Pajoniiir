idf_component_register(
    SRCS "monitor_pcm_link.c" "monitor_pcm_link_i2s.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common esp_driver_i2s esp_timer
    PRIV_REQUIRES esp_driver_gpio
)
