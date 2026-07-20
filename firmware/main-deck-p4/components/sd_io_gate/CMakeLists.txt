idf_component_register(SRCS "sd_io_gate.c"
                       INCLUDE_DIRS "include"
                       REQUIRES "esp_common"
                       PRIV_REQUIRES "freertos" "log")
