idf_component_register(
    SRCS "flx4_uac_descriptors.c" "flx4_uac_packetizer.c" "flx4_usb_audio.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common usb p4_audio_link
)
