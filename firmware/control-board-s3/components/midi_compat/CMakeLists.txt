idf_component_register(
    SRCS "midi_compat.c"
    INCLUDE_DIRS "include"
    REQUIRES panel_io log freertos espressif__esp_tinyusb espressif__tinyusb
)
