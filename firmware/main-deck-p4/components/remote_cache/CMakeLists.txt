idf_component_register(
    SRCS "remote_cache.c" "remote_cache_retry.c"
    INCLUDE_DIRS "include"
    REQUIRES log cdj_link_client cdj_link_protocol fatfs
)
