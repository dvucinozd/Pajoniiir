idf_component_register(
    SRCS "wifi_debug_log.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_event esp_netif esp_wifi freertos log nvs_flash
)
