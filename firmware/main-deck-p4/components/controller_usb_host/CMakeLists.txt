idf_component_register(
    # The P4-only product always enforces the fixed direct-root USB1 route and
    # closes the MIDI OUT producer gate before queue reset.
    SRCS "controller_usb_host_routed.c"
         "usb_midi_codec.c" "controller_midi_out_gate.c"
         "controller_usb_recovery_gate.c"
    INCLUDE_DIRS "include"
    REQUIRES log usb usb_host_manager controller_usb_audio
)
