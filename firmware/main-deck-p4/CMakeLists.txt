cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS
    "components"
    "${CMAKE_CURRENT_LIST_DIR}/../common/firmware_health"
    "${CMAKE_CURRENT_LIST_DIR}/../common/ota_manifest")
if(NOT DEFINED ENV{ESP_IDF_VERSION})
    set(ENV{ESP_IDF_VERSION} "5.5")
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(main-deck-p4)

# ── Workaround: IDF 5.4.x on ESP32-P4 defines uint32_t as 'unsigned long'
# but some IDF internals (esp_log_color.h) use '%u'. Downgrade to warning.
if("$ENV{ESP_IDF_VERSION}" VERSION_LESS "5.5")
    idf_build_set_property(COMPILE_OPTIONS "-Wno-error=format" APPEND)
endif()

# Enlarge LVGL's per-display invalidate-area buffer (default 32). When many
# small widgets invalidate in one refresh (e.g. the deck VU meters plus
# transport/beat widgets) the 32-slot buffer overflows and LVGL falls back to a
# full-screen redraw, which repaints over the direct-PPA waveforms and causes a
# visible flash. Must be global so every LVGL translation unit sees the same
# lv_display_t layout (the buffer is an inline array in that struct).
idf_build_set_property(COMPILE_DEFINITIONS "LV_INV_BUF_SIZE=64" APPEND)
