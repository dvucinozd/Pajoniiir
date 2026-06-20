idf_component_register(
    SRCS "audio_engine.c" "audio_mixer.c" "audio_pcm_ring.c" "audio_resampler.c" "audio_output_mixer.c" "audio_output_timing.c" "audio_fw_preload.c" "audio_fw_runtime.c" "audio_fw_task_context.c" "audio_fw_task_plan.c"
    INCLUDE_DIRS "include"
    REQUIRES log bsp_jc4880 esp_codec_dev esp_timer media_io_gate
    # minimp3.h is a single-header library included from the same directory.
    # Firmware audio: decode task + ES8311 I2S output via bsp_jc4880's codec dev.
)
