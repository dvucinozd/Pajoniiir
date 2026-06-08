idf_component_register(
    SRCS "cdj_link_client.c"
    INCLUDE_DIRS "include"
    REQUIRES log esp_http_client esp_timer cdj_link_protocol freertos lwip
)
