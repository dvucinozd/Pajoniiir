idf_component_register(
    SRCS "flx4_uac_descriptors.c" "flx4_uac_packetizer.c"
         "controller_audio_ring.c" "controller_audio_resampler.c"
         "controller_usb_audio_stream.c"
    INCLUDE_DIRS "include"
    REQUIRES log usb
)
