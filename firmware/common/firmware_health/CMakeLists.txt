idf_component_register(
    SRCS "firmware_health.c"
    INCLUDE_DIRS "include"
    REQUIRES app_update esp_partition esp_app_format log)
