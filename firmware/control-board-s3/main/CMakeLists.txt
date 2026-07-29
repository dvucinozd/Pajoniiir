set(APP_REQUIRES control_link control_state_reconciler log flx4_midi_host controller_profile_runtime p4_audio_link s3_debug_ap s3_ota wifi_debug_log firmware_health)

idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES ${APP_REQUIRES}
)
