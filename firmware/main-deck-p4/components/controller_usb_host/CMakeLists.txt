set(CONTROLLER_USB_HOST_OWNER "controller_usb_host.c")
if(CONFIG_PAJONIIIR_P4_LOCAL_CONTROLLER)
    # Compile the same transport through a narrow adapter that enforces the
    # fixed USB1 route and closes the MIDI OUT producer gate before queue reset.
    set(CONTROLLER_USB_HOST_OWNER "controller_usb_host_routed.c")
endif()

idf_component_register(
    SRCS ${CONTROLLER_USB_HOST_OWNER}
         "usb_midi_codec.c" "controller_midi_out_gate.c"
    INCLUDE_DIRS "include"
    REQUIRES log usb usb_host_manager controller_usb_audio
)
