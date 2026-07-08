idf_component_register(
    SRCS "app_main.c" "tcm_heap_guard.c"
    INCLUDE_DIRS "."
    REQUIRES control_link deck_core bsp_jc4880 library audio_engine ui usb_storage app_settings media_io_gate wifi_link web_server sd_diag_log controller_profile_manager log freertos monitor_pcm_link

)
