idf_component_register(
    SRCS "flx4_led_midi.c" "controller_led_runtime.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "../control_link/include"
    REQUIRES controller_profile_runtime controller_usb_host
)
