idf_component_register(
    SRCS "control_link_uart.c" "flx4_led_midi.c" "ctrl_bulk.c" "cp_xfer.c"
    INCLUDE_DIRS "include"
    REQUIRES driver log freertos esp_timer flx4_midi_host controller_profile_runtime status_led s3_debug_ap
)
