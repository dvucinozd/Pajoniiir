idf_component_register(
    SRCS "s3_debug_ap.c" "s3_debug_log_ring.c" "s3_ota_http.c" "s3_ota_upload_guard.c"
         "s3_debug_auth.c"
         "s3_debug_ap_netif_stage.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_event esp_http_server esp_netif esp_timer esp_wifi log nvs_flash s3_ota ota_manifest
)
