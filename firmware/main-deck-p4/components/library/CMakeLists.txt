idf_component_register(
    SRCS "rekordbox_pdb.c" "library.c"
         "rekordbox_anlz_fixed.c"
         "track_meta_cache.c"
    INCLUDE_DIRS "include"
    REQUIRES log heap freertos media_io_gate sd_io_gate esp_timer
)
