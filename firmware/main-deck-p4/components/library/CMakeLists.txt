idf_component_register(
    SRCS "library.c"
         "rekordbox_anlz.c"
         "rekordbox_pdb.c"
         "track_meta_cache.c"
    INCLUDE_DIRS "include"
    REQUIRES log heap freertos media_io_gate sd_diag_log
)
