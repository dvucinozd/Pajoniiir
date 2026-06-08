idf_component_register(
    SRCS "cdj_link_server.c"
    INCLUDE_DIRS "include"
    REQUIRES log esp_http_server esp_timer heap library cdj_link_protocol media_io_gate wifi_link freertos lwip
)
