idf_component_register(
    SRCS "../../../p4-core/components/usb_storage/usb_dwc_hal_compat.c"
    REQUIRES hal
)

target_link_options(${COMPONENT_LIB} INTERFACE
    "-Wl,--wrap=usb_dwc_hal_chan_decode_intr")
