idf_component_register(
    SRCS "wifi_link.c" "wifi_link_retry.c"
    INCLUDE_DIRS "include"
    REQUIRES log app_settings esp_event esp_netif esp_wifi esp_wifi_remote esp_hosted web_server service_log wifi_transition_lease
)
