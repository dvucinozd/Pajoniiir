idf_component_register(
    SRCS "controller_usb_host.c" "usb_midi_codec.c"
    INCLUDE_DIRS "include"
    REQUIRES log usb usb_host_manager
)
