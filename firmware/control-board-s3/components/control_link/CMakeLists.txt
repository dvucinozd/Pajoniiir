idf_component_register(
    SRCS "control_link_uart.c"
    INCLUDE_DIRS "include"
    REQUIRES panel_io driver log freertos esp_timer flx4_midi_host
)
