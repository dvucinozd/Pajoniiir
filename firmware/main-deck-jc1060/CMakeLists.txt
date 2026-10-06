cmake_minimum_required(VERSION 3.22)

include("${CMAKE_CURRENT_LIST_DIR}/../p4-core/cmake/project_version.cmake")
pajoniiir_resolve_version("M2*")

set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/../main-deck-p4/sdkconfig.defaults;${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")
# Explicit allow-list; never discover another board provider or entrypoint.
set(PAJONIIIR_SHARED_COMPONENTS
    p4_app app_settings audio_engine audio_recorder beat_jump board_adapter control_link
    controller_led_runtime controller_profile controller_profile_manager
    controller_profile_runtime controller_runtime controller_usb_audio controller_usb_host
    deck_core fatfs hot_cue_store library media_catalog media_identity media_io_gate
    p4_local_controller p4_ota p4_ota_pull p4_ota_pull_core sd_io_gate sd_idle_wait service_log ui
    usb_host_manager usb_storage web_server wifi_link wifi_transition_lease)

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../boards/jc1060")
foreach(component IN LISTS PAJONIIIR_SHARED_COMPONENTS)
    list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../p4-core/components/${component}")
endforeach()
list(APPEND EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../common/control_state_reconciler"
    "${CMAKE_CURRENT_LIST_DIR}/../common/firmware_health"
    "${CMAKE_CURRENT_LIST_DIR}/../common/ota_manifest"
    "${CMAKE_CURRENT_LIST_DIR}/../common/djlink"
    "${CMAKE_CURRENT_LIST_DIR}/../common/dj_link_core")
if(PAJONIIIR_STORAGE_EXPERIMENT)
    list(APPEND SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/../main-deck-p4/sdkconfig.storage_experiment")
    if(PAJONIIIR_STORAGE_VARIANT)
        list(APPEND SDKCONFIG_DEFAULTS "${PAJONIIIR_STORAGE_VARIANT}")
    endif()
endif()
if(PAJONIIIR_DJ_UI_PREVIEW)
    list(APPEND SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/../main-deck-p4/sdkconfig.dj_ui_preview")
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(main-deck-jc1060)
if(NOT CONFIG_PAJONIIIR_BOARD_JC1060)
    message(FATAL_ERROR "JC1060 target requires its board configuration")
endif()
if(NOT CONFIG_ESPTOOLPY_FLASHSIZE_16MB AND NOT CONFIG_ESPTOOLPY_FLASHSIZE_32MB
   AND NOT CONFIG_ESPTOOLPY_FLASHSIZE_64MB AND NOT CONFIG_ESPTOOLPY_FLASHSIZE_128MB)
    message(FATAL_ERROR "JC1060 production partition layout requires at least 16 MiB flash")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../p4-core/cmake/apply_espressif_usb_fifo_patch.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../p4-core/cmake/apply_espressif_usb_idle_recovery_patch.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../p4-core/cmake/apply_usb_host_msc_teardown_patch.cmake")
idf_build_set_property(COMPILE_DEFINITIONS "LV_INV_BUF_SIZE=64" APPEND)
add_custom_target(jc1060_binary_budget ALL
    COMMAND "${PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/../../tools/check_p4_binary_budget.py"
        --binary "${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin" --max-bytes 0x380000
        --output "${CMAKE_BINARY_DIR}/p4-binary-budget.json"
    DEPENDS gen_project_binary VERBATIM)
