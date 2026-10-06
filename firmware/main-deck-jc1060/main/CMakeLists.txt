idf_component_register(SRCS "app_main.c" INCLUDE_DIRS "."
    REQUIRES p4_app bsp_jc1060 board_adapter board_ethernet dj_link_service log)
