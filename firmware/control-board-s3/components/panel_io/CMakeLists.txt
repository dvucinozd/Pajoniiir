idf_component_register(
    SRCS "panel_io.c" "panel_encoder.c" "panel_pitch.c" "panel_leds.c"
    INCLUDE_DIRS "include"
    REQUIRES driver log freertos calibration
    PRIV_REQUIRES esp_adc esp_driver_pcnt esp_common
)
