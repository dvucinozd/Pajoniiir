idf_component_register(
    SRCS "flx4_map.c" "controller_runtime.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "../control_link/include"
    REQUIRES controller_usb_host
)
