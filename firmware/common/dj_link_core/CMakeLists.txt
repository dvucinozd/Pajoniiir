idf_component_register(SRCS "dj_link_discovery.c" "dj_link_udp.c" "dj_link_db.c"
    "dj_link_tcp.c" "dj_link_browse.c" "dj_link_cache.c" "dj_link_pdb.c" "dj_link_anlz.c"
    "dj_link_sync.c" INCLUDE_DIRS "include" REQUIRES djlink lwip media_identity deck_core)
