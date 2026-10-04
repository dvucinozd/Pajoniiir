idf_component_register(SRCS "app_main.c" "${CMAKE_CURRENT_LIST_DIR}/../../main-deck-p4/main/tcm_heap_guard.c"
    INCLUDE_DIRS "." "${CMAKE_CURRENT_LIST_DIR}/../../main-deck-p4/main"
    REQUIRES bsp_jc1060 board_adapter board_ethernet p4_ota_pull control_link deck_core
             library audio_engine audio_recorder ui usb_storage app_settings media_io_gate
             wifi_link web_server service_log controller_profile_manager p4_local_controller
             log freertos firmware_health p4_ota)
