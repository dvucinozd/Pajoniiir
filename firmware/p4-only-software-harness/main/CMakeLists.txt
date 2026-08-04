idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES controller_led_runtime controller_runtime controller_usb_host log usb_host_manager usb_host_msc
)
