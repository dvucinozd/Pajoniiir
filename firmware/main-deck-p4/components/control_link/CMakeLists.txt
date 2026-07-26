idf_component_register(
    SRCS "control_link_uart.c" "control_link_rx_stats.c"
         "flx4_led_snapshot.c" "ctrl_bulk.c" "cp_xfer.c"
    INCLUDE_DIRS "include"
    REQUIRES driver log freertos
)
