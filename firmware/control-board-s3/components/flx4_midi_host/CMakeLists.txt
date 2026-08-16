idf_component_register(
    SRCS "flx4_midi_host.c" "flx4_map.c" "midi_out_retry_state.c"
    INCLUDE_DIRS "include"
    REQUIRES log freertos usb control_link flx4_usb_audio status_led
)
