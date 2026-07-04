idf_component_register(
    SRCS "wifi_link.c"
    INCLUDE_DIRS "include"
    REQUIRES log esp_event esp_netif esp_wifi esp_wifi_remote esp_hosted
)
