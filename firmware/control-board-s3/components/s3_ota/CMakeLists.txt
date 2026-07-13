idf_component_register(
    SRCS "s3_ota.c" "s3_ota_policy.c"
    INCLUDE_DIRS "include"
    REQUIRES app_update firmware_health mbedtls ota_manifest
)
