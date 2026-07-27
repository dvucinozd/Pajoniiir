idf_component_register(
    SRCS "deck_core_live_led.c"
    INCLUDE_DIRS "include"
    REQUIRES control_link log freertos audio_engine hot_cue_store beat_jump library
)
