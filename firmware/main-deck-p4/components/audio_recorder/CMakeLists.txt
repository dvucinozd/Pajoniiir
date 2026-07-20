idf_component_register(SRCS "audio_recorder_wav.c"
                            "audio_recorder_ring.c"
                            "audio_recorder_writer.c"
                            "audio_recorder.c"
                       INCLUDE_DIRS "include"
                       REQUIRES "esp_common"
                       PRIV_REQUIRES "freertos" "log" "heap" "sd_io_gate")
