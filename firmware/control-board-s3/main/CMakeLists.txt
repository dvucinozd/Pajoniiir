idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES panel_io midi_compat control_link calibration log
)
