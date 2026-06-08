cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS "components")
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
