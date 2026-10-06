if(DEFINED IDF_VERSION AND NOT IDF_VERSION VERSION_EQUAL "6.0.2")
    message(FATAL_ERROR "SD idle compatibility layer requires explicit review on IDF upgrade")
endif()
idf_component_register(SRCS "sd_idle_wait.c" "sd_idle_wait_core.c" "sd_transfer_stats.c"
    INCLUDE_DIRS "include" REQUIRES sdmmc
    PRIV_REQUIRES esp_timer freertos WHOLE_ARCHIVE)
target_link_options(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=sdmmc_wait_for_idle" "-Wl,--wrap=sdmmc_read_sectors" "-Wl,--wrap=sdmmc_write_sectors")
