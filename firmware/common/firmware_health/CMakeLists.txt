idf_component_register(
    SRCS "firmware_health.c" "firmware_boot_gate.c" "p4_startup_gate.c" "firmware_resources.c"
    INCLUDE_DIRS "include"
    REQUIRES app_update esp_partition esp_app_format esp_system esp_timer heap freertos log)
