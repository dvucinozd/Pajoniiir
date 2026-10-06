if(CONFIG_PAJONIIIR_DJ_LINK_SERVICE AND NOT CONFIG_PAJONIIIR_BOARD_JC1060 AND
   NOT PAJONIIIR_LINK_EXPERIMENT)
    message(FATAL_ERROR "Network service requires an explicit non-production Link experiment")
endif()
if(CONFIG_USB_HOST_DWC_DMA_CAP_MEMORY_IN_PSRAM AND
   (NOT CONFIG_PAJONIIIR_BOARD_JC1060 OR NOT CONFIG_PAJONIIIR_SD_INTERNAL_BOUNCE OR
    NOT PAJONIIIR_STORAGE_EXPERIMENT))
    message(FATAL_ERROR "USB PSRAM DMA requires a JC1060 storage experiment and internal SD bounce")
endif()
idf_component_register(SRCS "board_capabilities.c" "board_sd_dma.c" "board_build_identity.c" INCLUDE_DIRS "include"
    REQUIRES esp_lcd esp_lcd_touch esp_codec_dev esp_driver_i2c
             esp_driver_i2s esp_driver_gpio esp_driver_ledc esp_driver_sdmmc
             fatfs sdmmc esp_hw_support
    PRIV_REQUIRES esp_timer)

find_package(Git REQUIRED)
execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
    RESULT_VARIABLE source_result OUTPUT_VARIABLE PAJONIIIR_SOURCE_SHA
    OUTPUT_STRIP_TRAILING_WHITESPACE)
string(LENGTH "${PAJONIIIR_SOURCE_SHA}" sha_bytes)
if(NOT source_result EQUAL 0 OR NOT sha_bytes EQUAL 40 OR
   NOT PAJONIIIR_SOURCE_SHA MATCHES "^[0-9a-f]+$")
    message(FATAL_ERROR "A verified Git source SHA is required")
endif()
execute_process(COMMAND "${GIT_EXECUTABLE}" status --porcelain
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
    RESULT_VARIABLE source_result OUTPUT_VARIABLE source_dirty)
if(NOT source_result EQUAL 0)
    message(FATAL_ERROR "Cannot verify source cleanliness")
endif()
set(PAJONIIIR_SOURCE_DIRTY 0)
if(source_dirty)
    set(PAJONIIIR_SOURCE_DIRTY 1)
endif()
configure_file(board_build_identity_generated.h.in board_build_identity_generated.h @ONLY)
configure_file(board_build_identity.json.in "${CMAKE_BINARY_DIR}/board_build_identity.json" @ONLY)
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
