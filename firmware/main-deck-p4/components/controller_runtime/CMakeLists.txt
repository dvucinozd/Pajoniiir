idf_component_register(
    SRCS "flx4_map.c" "controller_event_buffer.c" "controller_runtime.c"
    INCLUDE_DIRS "include"
    REQUIRES controller_profile controller_profile_runtime controller_usb_host
             control_link control_state_reconciler
)
