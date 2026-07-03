idf_component_register(
    SRCS "usb_storage.c" "usb_media_partition.c" "usb_media_mount.c"
    INCLUDE_DIRS "include"
    REQUIRES log usb usb_host_msc fatfs media_io_gate
)
