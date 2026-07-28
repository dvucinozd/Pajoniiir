# The recorder remains compiled out of release firmware until the independent
# STOP/finalize ownership audit and fault-injection plan are completed. Keep the
# guard here, next to the component, so an accidental sdkconfig change cannot
# silently ship the unfinished path.
if(CONFIG_AUDIO_RECORDER_ENABLED)
    message(FATAL_ERROR "Recorder is release-disabled pending physical SD fault-injection acceptance")
endif()

idf_component_register(SRCS "audio_recorder_wav.c"
                            "audio_recorder_finalize.c"
                            "audio_recorder_stop_gate.c"
                            "audio_recorder_ring.c"
                            "audio_recorder_writer.c"
                            "audio_recorder_sink.c"
                            "audio_recorder.c"
                       INCLUDE_DIRS "include"
                       REQUIRES "esp_common"
                       PRIV_REQUIRES "freertos" "log" "heap" "sd_io_gate" "service_log"
                                     "fatfs" "esp_timer" "esp_hw_support" "nvs_flash")
