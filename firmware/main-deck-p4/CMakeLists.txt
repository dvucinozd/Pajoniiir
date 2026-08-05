cmake_minimum_required(VERSION 3.22)
set(EXTRA_COMPONENT_DIRS
    "components"
    "${CMAKE_CURRENT_LIST_DIR}/../common/firmware_health"
    "${CMAKE_CURRENT_LIST_DIR}/../common/ota_manifest"
    "${CMAKE_CURRENT_LIST_DIR}/../common/control_state_reconciler")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(main-deck-p4)

# Enlarge LVGL's per-display invalidate-area buffer (default 32). When many
# small widgets invalidate in one refresh (e.g. the deck VU meters plus
# transport/beat widgets) the 32-slot buffer overflows and LVGL falls back to a
# full-screen redraw, which repaints over the direct-PPA waveforms and causes a
# visible flash. Must be global so every LVGL translation unit sees the same
# lv_display_t layout (the buffer is an inline array in that struct).
idf_build_set_property(COMPILE_DEFINITIONS "LV_INV_BUF_SIZE=64" APPEND)

# The migration-only P4-local image is held below 0x380000 bytes, preserving
# 512 KiB of margin inside the 0x400000 application partition. The default
# P4/S3 product build does not create this additional target.
if(CONFIG_PAJONIIIR_P4_LOCAL_CONTROLLER)
    add_custom_target(p4_local_binary_budget ALL
        COMMAND "${PYTHON}"
            "${CMAKE_CURRENT_LIST_DIR}/../../tools/check_p4_binary_budget.py"
            --binary "${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin"
            --max-bytes 0x380000
            --output "${CMAKE_BINARY_DIR}/p4-binary-budget.json"
        DEPENDS gen_project_binary
        COMMENT "Checking P4-local migration binary budget"
        VERBATIM)
endif()
