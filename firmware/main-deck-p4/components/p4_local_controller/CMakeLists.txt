idf_component_register(
    SRCS "p4_local_controller.c"
    INCLUDE_DIRS "include"
    REQUIRES control_link usb_host_manager controller_usb_host
             controller_runtime controller_led_runtime
             controller_profile_runtime controller_profile_manager
             log freertos
)
