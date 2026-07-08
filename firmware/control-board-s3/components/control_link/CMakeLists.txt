idf_component_register(
    SRCS "control_link_uart.c" "flx4_led_midi.c" "ctrl_bulk.c"
    INCLUDE_DIRS "include"
    REQUIRES panel_io driver log freertos esp_timer flx4_midi_host status_led s3_debug_ap
)
