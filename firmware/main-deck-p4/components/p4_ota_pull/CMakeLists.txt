idf_component_register(
    SRCS "p4_ota_pull.c"
    INCLUDE_DIRS "include"
    REQUIRES p4_ota_pull_core app_settings wifi_link esp_http_client esp-tls app_update
)