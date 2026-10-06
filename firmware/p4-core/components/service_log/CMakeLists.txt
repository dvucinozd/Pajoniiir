idf_component_register(SRCS "service_log_format.c"
                            "service_log.c"
                       INCLUDE_DIRS "include"
                       REQUIRES "esp_common"
                       PRIV_REQUIRES "freertos" "log" "esp_timer" "nvs_flash" "sd_io_gate")
