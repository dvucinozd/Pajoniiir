idf_component_register(SRCS "dj_link_service.c" "dj_link_download.c" INCLUDE_DIRS "include"
    REQUIRES board_adapter dj_link_core app_settings media_catalog sd_io_gate deck_core
        esp_netif esp_timer esp_hw_support heap freertos log)
