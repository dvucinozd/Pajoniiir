idf_component_register(
    SRCS "control_link_uart.c" "flx4_led_snapshot.c"
    INCLUDE_DIRS "include"
    REQUIRES driver log freertos
)
