idf_component_register(SRCS "board_ethernet.c" INCLUDE_DIRS "include"
    REQUIRES esp_eth esp_netif esp_event esp_hw_support log)
