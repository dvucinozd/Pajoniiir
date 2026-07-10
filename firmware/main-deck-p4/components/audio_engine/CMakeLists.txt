idf_component_register(
    SRCS "audio_engine.c" "audio_diag.c" "audio_eq.c" "audio_filter.c" "audio_smart_cfx.c" "audio_mixer.c" "audio_pcm_ring.c" "audio_resampler.c" "audio_delay_fx.c" "audio_flanger_fx.c" "audio_pad_fx.c" "audio_output_mixer.c" "audio_output_timing.c" "audio_fw_preload.c" "audio_fw_runtime.c" "audio_fw_task_context.c" "audio_fw_task_plan.c" "audio_format.c" "audio_decoder.c" "audio_wav_decoder.c" "audio_flac_decoder.c"
    INCLUDE_DIRS "include"
    REQUIRES log bsp_jc4880 esp_codec_dev esp_timer media_io_gate monitor_pcm_link
    # minimp3.h and dr_flac.h are single-header libraries included from this dir.
    # audio_flac_decoder.c holds the one DR_FLAC_IMPLEMENTATION.
    # Firmware audio: decode task + PCM5102A I2S MAIN out via bsp_jc4880.
)
