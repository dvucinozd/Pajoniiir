idf_component_register(
    SRCS "media_catalog.c"
    INCLUDE_DIRS "include"
    REQUIRES log heap library cdj_link_client cdj_link_protocol remote_cache
)
