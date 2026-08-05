if(CONFIG_PAJONIIIR_P4_LOCAL_CONTROLLER)
    set(P4_LOCAL_CONTROLLER_SOURCE "p4_local_controller.c")
else()
    set(P4_LOCAL_CONTROLLER_SOURCE "p4_local_controller_disabled.c")
endif()

idf_component_register(
    SRCS ${P4_LOCAL_CONTROLLER_SOURCE}
    INCLUDE_DIRS "include"
    REQUIRES control_link usb_storage usb_host_manager controller_usb_host
             controller_runtime controller_led_runtime
             controller_profile_runtime controller_profile_manager
             sd_io_gate log freertos
    WHOLE_ARCHIVE
)

if(CONFIG_PAJONIIIR_P4_LOCAL_CONTROLLER)
    target_link_options(${COMPONENT_LIB} INTERFACE
        "-Wl,--wrap=control_link_init"
        "-Wl,--wrap=control_link_send_led_deck"
        "-Wl,--wrap=usb_storage_init")
endif()
