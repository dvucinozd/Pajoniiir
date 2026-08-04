idf_component_register(
    SRCS "dual_usb_spike.c" "usb_midi_probe.c"
    INCLUDE_DIRS "."
    REQUIRES esp_timer heap log usb usb_host_msc usb_dwc_compat
)
