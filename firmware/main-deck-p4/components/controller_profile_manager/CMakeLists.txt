idf_component_register(SRCS "controller_profile_manager.c"
                       INCLUDE_DIRS "include"
                       REQUIRES control_link freertos service_log sd_io_gate)
