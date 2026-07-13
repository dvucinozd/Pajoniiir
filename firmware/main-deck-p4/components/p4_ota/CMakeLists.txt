idf_component_register(
    SRCS "p4_ota.c" "p4_ota_policy.c"
    INCLUDE_DIRS "include"
    REQUIRES app_update firmware_health log)
