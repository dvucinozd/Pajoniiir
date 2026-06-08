idf_component_register(
    SRCS "usb_storage.c"
    INCLUDE_DIRS "include"
    REQUIRES log usb usb_host_msc fatfs media_io_gate
)
