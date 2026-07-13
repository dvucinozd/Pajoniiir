idf_component_register(
    SRCS "usb_storage.c" "usb_media_partition.c" "usb_media_mount.c"
         "usb_dwc_hal_compat.c"
    INCLUDE_DIRS "include"
    REQUIRES log usb usb_host_msc fatfs media_io_gate hal
    WHOLE_ARCHIVE
)

# ESP-IDF v5.5's DWC HAL asserts that every channel error includes CHHLTD,
# although its own contract documents BNAINTR as the exception. Wrap only the
# interrupt decoder so the project-local compatibility implementation can route
# that expected condition into the HCD's existing recoverable pipe-error path.
target_link_options(${COMPONENT_LIB} INTERFACE
    "-Wl,--wrap=usb_dwc_hal_chan_decode_intr")
