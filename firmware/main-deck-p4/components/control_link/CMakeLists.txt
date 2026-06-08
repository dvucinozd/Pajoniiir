idf_component_register(
    SRCS "control_link_uart.c"
    INCLUDE_DIRS "include"
    REQUIRES driver log freertos
)
