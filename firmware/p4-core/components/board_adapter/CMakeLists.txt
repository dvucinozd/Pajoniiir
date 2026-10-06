if(CONFIG_USB_HOST_DWC_DMA_CAP_MEMORY_IN_PSRAM AND
   (NOT CONFIG_PAJONIIIR_BOARD_JC1060 OR NOT CONFIG_PAJONIIIR_SD_INTERNAL_BOUNCE OR
    NOT PAJONIIIR_STORAGE_EXPERIMENT))
    message(FATAL_ERROR "USB PSRAM DMA requires a JC1060 storage experiment and internal SD bounce")
endif()
idf_component_register(SRCS "board_capabilities.c" "board_sd_dma.c" INCLUDE_DIRS "include"
    REQUIRES esp_lcd esp_lcd_touch esp_codec_dev esp_driver_i2c
             esp_driver_i2s esp_driver_gpio esp_driver_ledc esp_driver_sdmmc
             fatfs sdmmc esp_hw_support
    PRIV_REQUIRES esp_timer)
