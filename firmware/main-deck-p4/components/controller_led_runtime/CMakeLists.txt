idf_component_register(
    SRCS "flx4_led_midi.c" "controller_led_runtime.c"
    INCLUDE_DIRS "include"
    REQUIRES controller_profile_runtime controller_usb_host control_link
)
