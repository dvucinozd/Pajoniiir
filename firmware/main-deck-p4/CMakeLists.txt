cmake_minimum_required(VERSION 3.22)

# Resolve the application version before ESP-IDF generates esp_app_desc_t.
# Derived release tags such as M2.2-13-ge0f9add are distribution labels, not
# version ancestry roots; excluding them prevents chained git-describe output.
# The explicit byte limit turns a future overlong tag into a configure error
# instead of allowing the fixed 32-byte app descriptor field to truncate it.
find_package(Git REQUIRED)
execute_process(
    COMMAND "${GIT_EXECUTABLE}" describe --always --tags --dirty --exclude "*-g*"
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
    RESULT_VARIABLE PAJONIIIR_GIT_DESCRIBE_RESULT
    OUTPUT_VARIABLE PROJECT_VER
    ERROR_VARIABLE PAJONIIIR_GIT_DESCRIBE_ERROR
    OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT PAJONIIIR_GIT_DESCRIBE_RESULT EQUAL 0)
    string(STRIP "${PAJONIIIR_GIT_DESCRIBE_ERROR}" PAJONIIIR_GIT_DESCRIBE_ERROR)
    message(FATAL_ERROR
        "Unable to resolve PROJECT_VER with git describe: "
        "${PAJONIIIR_GIT_DESCRIBE_ERROR}")
endif()
string(LENGTH "${PROJECT_VER}" PAJONIIIR_PROJECT_VER_BYTES)
if(PAJONIIIR_PROJECT_VER_BYTES GREATER 31)
    message(FATAL_ERROR
        "PROJECT_VER '${PROJECT_VER}' is ${PAJONIIIR_PROJECT_VER_BYTES} bytes; "
        "ESP-IDF app descriptors allow at most 31 bytes")
endif()

set(EXTRA_COMPONENT_DIRS
    "components"
    "${CMAKE_CURRENT_LIST_DIR}/../common/control_state_reconciler"
    "${CMAKE_CURRENT_LIST_DIR}/../common/firmware_health"
    "${CMAKE_CURRENT_LIST_DIR}/../common/ota_manifest")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(main-deck-p4)

# The dual-root image needs bulk-oriented HS FIFOs for storage and
# periodic-OUT-oriented FS FIFOs for controller UAC. Apply the pinned,
# fail-closed patch after Component Manager has materialized esp-usb.
include("${CMAKE_CURRENT_LIST_DIR}/cmake/apply_espressif_usb_fifo_patch.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/apply_espressif_usb_idle_recovery_patch.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/apply_usb_host_msc_teardown_patch.cmake")

# Enlarge LVGL's per-display invalidate-area buffer (default 32). When many
# small widgets invalidate in one refresh (e.g. the deck VU meters plus
# transport/beat widgets) the 32-slot buffer overflows and LVGL falls back to a
# full-screen redraw, which repaints over the direct-PPA waveforms and causes a
# visible flash. Must be global so every LVGL translation unit sees the same
# lv_display_t layout (the buffer is an inline array in that struct).
idf_build_set_property(COMPILE_DEFINITIONS "LV_INV_BUF_SIZE=64" APPEND)

# The P4-only product image is held below 0x380000 bytes, preserving 512 KiB
# of margin inside the 0x400000 application partition.
add_custom_target(p4_binary_budget ALL
    COMMAND "${PYTHON}"
        "${CMAKE_CURRENT_LIST_DIR}/../../tools/check_p4_binary_budget.py"
        --binary "${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin"
        --max-bytes 0x380000
        --output "${CMAKE_BINARY_DIR}/p4-binary-budget.json"
    DEPENDS gen_project_binary
    COMMENT "Checking P4-only product binary budget"
    VERBATIM)
