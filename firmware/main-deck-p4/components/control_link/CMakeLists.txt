idf_component_register(
    SRCS "control_link_local.c" "flx4_led_snapshot.c"
    INCLUDE_DIRS "include"
    REQUIRES log freertos
)
