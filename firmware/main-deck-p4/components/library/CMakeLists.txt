idf_component_register(
    SRCS "rekordbox_pdb.c" "library_duration_fixed.c"
         "rekordbox_anlz_fixed.c"
         "track_meta_cache_fixed.c"
    INCLUDE_DIRS "include"
    REQUIRES log heap freertos media_io_gate sd_io_gate esp_timer
)
