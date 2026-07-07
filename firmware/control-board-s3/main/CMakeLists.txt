set(APP_REQUIRES panel_io control_link calibration log flx4_midi_host p4_audio_link s3_debug_ap wifi_debug_log)

if(NOT CONFIG_DDJ_FLX4_HOST_MODE)
    list(APPEND APP_REQUIRES midi_compat)
endif()

idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES ${APP_REQUIRES}
)
