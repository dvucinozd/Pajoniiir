idf_component_register(
    SRCS "ui.c"
         "ui_deck_anlz_store.c"
         "ui_beat_indicator.c"
    INCLUDE_DIRS "include"
    REQUIRES log deck_core library bsp_jc4880 audio_engine app_settings control_link media_catalog cdj_link_client remote_cache sd_diag_log wifi_link hot_cue_store fatfs esp_lcd esp_lcd_touch esp_timer esp_driver_gpio esp_driver_ppa esp_mm idf::lvgl__lvgl
)
