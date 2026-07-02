idf_component_register(
    SRCS "p4_audio_link.c" "p4_audio_link_i2s.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common esp_driver_i2s
)
