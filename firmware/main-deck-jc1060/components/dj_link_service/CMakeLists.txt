idf_component_register(SRCS "dj_link_service.c" INCLUDE_DIRS "include"
    REQUIRES board_ethernet dj_link_core app_settings esp_netif esp_timer heap freertos log)
